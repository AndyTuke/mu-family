#include "Plugin/PluginProcessor.h"
#include "Plugin/PluginEditor.h"

namespace mu_on
{

juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto f = [](float lo, float hi, float step) { return NormalisableRange<float>(lo, hi, step); };

    static const char* kNames[kNumChannels] = { "Kick", "Bass", "Hat", "Snare" };

    // ── Mixer channel strips — one per instrument lane (shared `ch{N}_` binding the
    //    MixerChannel / MixerOverlay use). The Bass lane (ch1) pre-wires its sidechain
    //    SOURCE to the Kick lane (ch0) so bass-ducks-kick works out of the box: the
    //    shared MixerEngine does the ducking; only the defaults are product-specific.
    for (int i = 0; i < kNumChannels; ++i)
    {
        const String c = "ch" + String(i) + "_";
        const String n = String(kNames[i]) + " Ch ";

        const bool isBass = (i == Bass);
        const int   scSrcDefault = isBass ? (Kick + 1) : 0;   // param 1..8 = ch0..7; +1 maps Kick→1
        const float scAmtDefault = isBass ? 0.4f : 0.0f;
        const float scRelDefault = isBass ? 120.0f : 100.0f;

        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{c+"lvl",  1}, n+"Level", f(0.0f, 1.0f, 0.001f), 1.0f));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{c+"pan",  1}, n+"Pan",   f(-1.0f, 1.0f, 0.001f), 0.0f));
        layout.add(std::make_unique<AudioParameterBool> (ParameterID{c+"mute", 1}, n+"Mute",  false));
        layout.add(std::make_unique<AudioParameterBool> (ParameterID{c+"solo", 1}, n+"Solo",  false));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{c+"sendEff", 1}, n+"Send Eff", f(0.0f, 1.0f, 0.001f), 0.0f));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{c+"sendDly", 1}, n+"Send Dly", f(0.0f, 1.0f, 0.001f), 0.0f));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{c+"sendRev", 1}, n+"Send Rev", f(0.0f, 1.0f, 0.001f), 0.0f));
        layout.add(std::make_unique<AudioParameterInt>  (ParameterID{c+"scSrc",   1}, n+"SC Src",  0, 9, scSrcDefault));  // 0=off, 1-8=ch0-7, 9=ext
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{c+"scAmt",   1}, n+"SC Amount", f(0.0f, 1.0f, 0.001f), scAmtDefault));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{c+"scAtk",   1}, n+"SC Attack", f(1.0f, 500.0f, 0.1f), 5.0f));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{c+"scRel",   1}, n+"SC Release", f(10.0f, 2000.0f, 1.0f), scRelDefault));
        layout.add(std::make_unique<AudioParameterInt>  (ParameterID{c+"outBus",  1}, n+"Output Bus", 0, 8, 0));
    }

    // ── Sequencer (global groove controls) ────────────────────────────────────
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"seq_swing",  1}, "Swing",  f(0.0f, 1.0f, 0.001f), 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"seq_accent", 1}, "Accent", f(0.0f, 1.0f, 0.001f), 1.0f));

    // ── Kick engine (synthesis) ───────────────────────────────────────────────
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"k_tune",  1}, "Kick Tune",       f(30.0f, 120.0f, 0.1f), 50.0f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"k_ptch",  1}, "Kick Pitch Amt",  f(0.0f, 600.0f, 1.0f), 220.0f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"k_pdec",  1}, "Kick Pitch Decay",f(5.0f, 200.0f, 0.1f), 50.0f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"k_adec",  1}, "Kick Decay",      f(20.0f, 800.0f, 1.0f), 180.0f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"k_drive", 1}, "Kick Drive",      f(0.0f, 1.0f, 0.001f), 0.2f));

    // ── Bass engine (deep synth — the focus) ──────────────────────────────────
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID{"b_wave", 1}, "Bass Wave", juce::StringArray{ "Sine", "Saw", "Square" }, 0));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"b_sub",   1}, "Bass Sub",        f(0.0f, 1.0f, 0.001f), 0.5f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"b_tune",  1}, "Bass Tune",       f(20.0f, 120.0f, 0.1f), 41.2f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"b_cut",   1}, "Bass Cutoff",     f(40.0f, 4000.0f, 1.0f), 600.0f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"b_res",   1}, "Bass Resonance",  f(0.0f, 1.0f, 0.001f), 0.2f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"b_env",   1}, "Bass Filter Env", f(0.0f, 1.0f, 0.001f), 0.4f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"b_edec",  1}, "Bass Env Decay",  f(10.0f, 1000.0f, 1.0f), 180.0f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"b_atk",   1}, "Bass Attack",     f(0.5f, 200.0f, 0.1f), 2.0f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"b_dec",   1}, "Bass Decay",      f(20.0f, 1500.0f, 1.0f), 200.0f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"b_sus",   1}, "Bass Sustain",    f(0.0f, 1.0f, 0.001f), 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"b_drive", 1}, "Bass Drive",      f(0.0f, 1.0f, 0.001f), 0.2f));

    // ── Hat / Snare (sample channels) ─────────────────────────────────────────
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"h_tune", 1}, "Hat Tune",   f(-12.0f, 12.0f, 0.1f), 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"h_dec",  1}, "Hat Decay",  f(10.0f, 400.0f, 1.0f), 60.0f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"s_tune", 1}, "Snare Tune", f(-12.0f, 12.0f, 0.1f), 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"s_dec",  1}, "Snare Decay",f(20.0f, 600.0f, 1.0f), 160.0f));

    // ── Shared global FX rack + returns + master (mu-core) ────────────────────
    mu_mixfx::addGlobalFxParams(layout);

    return layout;
}

PluginProcessor::PluginProcessor()
    : ProcessorBase(BusesProperties()
                        .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)
                        .withOutput("Output",    juce::AudioChannelSet::stereo(), true),
                    createParameterLayout(),
                    juce::Identifier("MuOnState"))
{
    // Each channel renders its instrument engine; the shared mixer then applies the strip,
    // the bass→kick sidechain, and the master mix.
    renderChannelCb = [this](int ch, juce::AudioBuffer<float>& buf, int n) { grooveVoices.render(ch, buf, n); };

    // A default groove so a fresh instance plays something immediately.
    stepPattern.loadDefaultGroove();

    seqSwingParam  = apvts.getRawParameterValue("seq_swing");
    seqAccentParam = apvts.getRawParameterValue("seq_accent");
    grooveVoices.cacheParams(apvts);

    registerFxListeners();
    syncAllFxParams();   // JUCE doesn't fire parameterChanged on construction
}

PluginProcessor::~PluginProcessor()
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
        {
            const juce::String id = rp->getParameterID();
            if (id.startsWith("ch") || mu_mixfx::isGlobalFxParamId(id))
                apvts.removeParameterListener(id, this);
        }
}

void PluginProcessor::registerFxListeners()
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
        {
            const juce::String id = rp->getParameterID();
            if (id.startsWith("ch") || mu_mixfx::isGlobalFxParamId(id))
                apvts.addParameterListener(id, this);
        }
}

void PluginProcessor::syncAllFxParams()
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
        {
            const juce::String id = rp->getParameterID();
            if (id.startsWith("ch") || mu_mixfx::isGlobalFxParamId(id))
                if (auto* a = apvts.getRawParameterValue(id))
                    syncGlobalFxParam(id, a->load());
        }
}

void PluginProcessor::parameterChanged(const juce::String& id, float v)
{
    if (id.startsWith("ch") || mu_mixfx::isGlobalFxParamId(id))
        syncGlobalFxParam(id, v);
}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    sequencer.prepare(sampleRate);
    grooveVoices.prepare(sampleRate, samplesPerBlock);
    mixerEngine.prepare(sampleRate, samplesPerBlock);
    fxChain.prepare(sampleRate, samplesPerBlock);
}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& ins = layouts.inputBuses;
    if (ins.size() > 1) return false;
    if (ins.size() == 1 && ins.getReference(0) != juce::AudioChannelSet::stereo()
                        && ins.getReference(0) != juce::AudioChannelSet::disabled())
        return false;
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    const double bpm       = internalBpm.load(std::memory_order_relaxed);
    const double beatStart = internalBeatPos.load(std::memory_order_relaxed);

    // Refresh engine params from the APVTS, then clock the 909 sequencer for this block
    // (before the render so a step's engine is armed for this same block). Each fired lane
    // triggers its engine and bumps a counter the editor polls to pulse the sidebar.
    grooveVoices.applyParams();
    if (playing.load(std::memory_order_relaxed))
    {
        sequencer.setSwing (seqSwingParam  ? seqSwingParam->load()  : 0.0f);
        sequencer.setAccentVelocity(seqAccentParam ? seqAccentParam->load() : 1.0f);
        sequencer.process(beatStart, numSamples, bpm,
                          [this](int track, float vel, int /*off*/)
                          {
                              grooveVoices.trigger(track, vel);
                              if (track >= 0 && track < kNumChannels)
                                  triggers[(size_t) track].fetch_add(1, std::memory_order_relaxed);
                          });
    }

    // Supply the external DAW sidechain bus to the mixer (null when bus is inactive).
    mixerEngine.setExternalSidechain(nullptr, nullptr);
    if (getBusCount(true) > 0)
        if (auto* scBus = getBus(true, 0); scBus && scBus->isEnabled())
        {
            auto scBuf = getBusBuffer(buffer, true, 0);
            const int nCh = scBuf.getNumChannels();
            if (nCh >= 1)
                mixerEngine.setExternalSidechain(
                    scBuf.getReadPointer(0),
                    nCh >= 2 ? scBuf.getReadPointer(1) : scBuf.getReadPointer(0));
        }

    // Render (silent for now) → mixer through the shared path (engine→insert→mixer).
    processCoreBlock(buffer, nullptr, kNumChannels, numSamples, bpm,
                     nullptr, nullptr, nullptr, &renderChannelCb);

    // Advance the free-running transport while playing (the sequencer will read this).
    if (playing.load(std::memory_order_relaxed))
    {
        const double beatsPerSample = (bpm / 60.0) / currentSampleRate;
        internalBeatPos.store(internalBeatPos.load(std::memory_order_relaxed)
                              + beatsPerSample * (double) numSamples,
                              std::memory_order_relaxed);
    }
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor(*this);
}

void PluginProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    stepPattern.serialise(state);   // ride the 909 grid along in the state tree
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void PluginProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
        {
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
            stepPattern.deserialise(apvts.state);
            syncAllFxParams();   // re-seed mixer/FX (unchanged values skip listeners)
        }
}

juce::File PluginProcessor::getContentDir() const
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
               .getChildFile("TDP").getChildFile("muOn");
}

juce::File PluginProcessor::getPresetsDir()       const { return getContentDir().getChildFile("Presets"); }
juce::File PluginProcessor::getPerSlotPresetDir() const { return getContentDir().getChildFile("Tracks"); }

} // namespace mu_on

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new mu_on::PluginProcessor();
}
