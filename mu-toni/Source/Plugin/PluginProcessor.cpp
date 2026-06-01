#include "Plugin/PluginProcessor.h"
#include "Plugin/PluginEditor.h"

namespace mu_toni
{

juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto f = [](float lo, float hi, float step) { return NormalisableRange<float>(lo, hi, step); };

    // ── Mixer channel strips (one per placeholder layer) — shared `ch{N}_`
    //    binding the MixerChannel / MixerOverlay use: level/pan/mute/solo + FX
    //    sends + sidechain + output bus. Synced via ProcessorBase::syncGlobalFxParam.
    for (int i = 0; i < kNumChannels; ++i)
    {
        const String c = "ch" + String(i) + "_";
        const String n = "Layer " + String(i + 1) + " Ch ";
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{c+"lvl",  1}, n+"Level", f(0.0f, 1.0f, 0.001f), 1.0f));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{c+"pan",  1}, n+"Pan",   f(-1.0f, 1.0f, 0.001f), 0.0f));
        layout.add(std::make_unique<AudioParameterBool> (ParameterID{c+"mute", 1}, n+"Mute",  false));
        layout.add(std::make_unique<AudioParameterBool> (ParameterID{c+"solo", 1}, n+"Solo",  false));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{c+"sendEff", 1}, n+"Send Eff", f(0.0f, 1.0f, 0.001f), 0.0f));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{c+"sendDly", 1}, n+"Send Dly", f(0.0f, 1.0f, 0.001f), 0.0f));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{c+"sendRev", 1}, n+"Send Rev", f(0.0f, 1.0f, 0.001f), 0.0f));
        layout.add(std::make_unique<AudioParameterInt>  (ParameterID{c+"scSrc",   1}, n+"SC Src",  0, 9, 0));  // 0=off, 1-8=ch0-ch7, 9=ext DAW bus
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{c+"scAmt",   1}, n+"SC Amount", f(0.0f, 1.0f, 0.001f), 0.0f));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{c+"scAtk",   1}, n+"SC Attack", f(1.0f, 500.0f, 0.1f), 5.0f));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{c+"scRel",   1}, n+"SC Release", f(10.0f, 2000.0f, 1.0f), 100.0f));
        layout.add(std::make_unique<AudioParameterInt>  (ParameterID{c+"outBus",  1}, n+"Output Bus", 0, 8, 0));
    }

    // ── Shared global FX rack + returns + master (mu-core) ────────────────────
    mu_mixfx::addGlobalFxParams(layout);

    return layout;
}

PluginProcessor::PluginProcessor()
    : ProcessorBase(BusesProperties()
                        .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)
                        .withOutput("Output",    juce::AudioChannelSet::stereo(), true),
                    createParameterLayout(),
                    juce::Identifier("MuToniState"))
{
    // Silent channel render (no synth engine yet) — the shared mixer still applies
    // the strip + master mix, so the MixerOverlay + VU meters are fully wired.
    renderChannelCb = [](int, juce::AudioBuffer<float>& buf, int) { buf.clear(); };

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
    mixerEngine.prepare(sampleRate, samplesPerBlock);
    fxChain.prepare(sampleRate, samplesPerBlock);
}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Sidechain input: at most one, must be stereo or disabled.
    const auto& ins = layouts.inputBuses;
    if (ins.size() > 1) return false;
    if (ins.size() == 1 && ins.getReference(0) != juce::AudioChannelSet::stereo()
                        && ins.getReference(0) != juce::AudioChannelSet::disabled())
        return false;
    // Main output: must be stereo.
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    const double bpm = internalBpm.load(std::memory_order_relaxed);

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

    // Render (silent) → mixer through the shared path (engine→insert→mixer). With
    // no engine the render hook clears each channel; the mixer owns the strip +
    // master mix so the mixer panel + VU meters behave.
    processCoreBlock(buffer, nullptr, kNumChannels, numSamples, bpm,
                     nullptr, nullptr, nullptr, &renderChannelCb);

    // Advance the free-running transport while playing (nothing consumes it yet).
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
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void PluginProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
        {
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
            syncAllFxParams();   // re-seed mixer/FX (unchanged values skip listeners)
        }
}

juce::File PluginProcessor::getContentDir() const
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
               .getChildFile("TDP").getChildFile("muToni");
}

juce::File PluginProcessor::getPresetsDir()       const { return getContentDir().getChildFile("Presets"); }
juce::File PluginProcessor::getPerSlotPresetDir() const { return getContentDir().getChildFile("Layers"); }

} // namespace mu_toni

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new mu_toni::PluginProcessor();
}
