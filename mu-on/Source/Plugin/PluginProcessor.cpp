#include "Plugin/PluginProcessor.h"
#include "Plugin/TransportResolver.h"   // mu-core: the family transport rule
#include "Plugin/PluginEditor.h"
#include "Plugin/HostTransport.h"          // mu-core: DAW / mu-link transport read
#include "Modulation/MuOnModDest.h"
#include "Modulation/ModulatorSerialise.h"   // mu-core: shared modulator (de)serialise

namespace mu_on
{

juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto f = [](float lo, float hi, float step) { return NormalisableRange<float>(lo, hi, step); };

    static const char* kNames[kNumChannels] = { "Kick", "Bass", "Hat", "Snare", "Rumble" };

    // ── Mixer channel strips — one per instrument lane (shared `ch{N}_` binding the
    //    MixerChannel / MixerOverlay use). The Bass lane (ch1) pre-wires its sidechain
    //    SOURCE to the Kick lane (ch0) so bass-ducks-kick works out of the box: the
    //    shared MixerEngine does the ducking; only the defaults are product-specific.
    for (int i = 0; i < kNumChannels; ++i)
    {
        const String c = "ch" + String(i) + "_";
        const String n = String(kNames[i]) + " Ch ";

        const bool isBass = (i == Bass);
        // Bass + Rumble pre-wire their sidechain SOURCE to the Kick (bass ducks the kick out of
        // the box; Rumble exposes it for optional kick-pumping — amount left at 0 by default).
        const bool fromKick = isBass || (i == Rumble);
        mu_mixfx::ChannelStripOptions opts;
        opts.scSrc = fromKick ? (Kick + 1) : 0;   // param 1..8 = ch0..7; +1 maps Kick→1
        opts.scAmt = isBass ? 0.4f : 0.0f;
        opts.scRel = isBass ? 120.0f : 100.0f;
        mu_mixfx::addChannelStripParams(layout, c, n, opts);
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

    // ── Rumble engine (processes the Kick feed: drive → delays → reverb → env → filter) ──
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"r_drive", 1}, "Rumble Drive",   f(0.0f, 1.0f, 0.001f), 0.4f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"r_d1",    1}, "Rumble 1/16",    f(0.0f, 1.0f, 0.001f), 0.5f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"r_d2",    1}, "Rumble 2/16",    f(0.0f, 1.0f, 0.001f), 0.35f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"r_d3",    1}, "Rumble 3/16",    f(0.0f, 1.0f, 0.001f), 0.25f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"r_size",  1}, "Rumble Rev Size",f(0.0f, 1.0f, 0.001f), 0.7f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"r_revmix",1}, "Rumble Rev Mix", f(0.0f, 1.0f, 0.001f), 0.5f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"r_revlp", 1}, "Rumble Rev LP",
                juce::NormalisableRange<float>(200.0f, 18000.0f, 1.0f, 0.3f), 1200.0f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"r_cut",   1}, "Rumble Cutoff",
                juce::NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 0.3f), 800.0f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"r_res",   1}, "Rumble Resonance",f(0.0f, 1.0f, 0.001f), 0.2f));

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

    // Rumble bar-volume envelope: a smooth, unipolar curve drawn in the grid slot for the
    // Rumble lane. Default = flat at full level (no shaping until the user draws one).
    rumbleEnv.mode           = ControlSequence::Mode::Smooth;
    rumbleEnv.polarity       = ControlSequence::Polarity::Unipolar;
    rumbleEnv.loopNoteValue  = NoteValue::Quarter;
    rumbleEnv.loopNoteMod    = NoteMod::None;
    rumbleEnv.loopMultiplier = 4;   // 1 bar (4 beats) so it cycles once per bar
    rumbleEnv.curvePoints    = { { 0.0f, 1.0f }, { 1.0f, 1.0f } };   // flat full by default
    grooveVoices.setRumbleEnv(&rumbleEnv, &rumbleEnvLock);

    // Tag each modulation slot with its lane identity (name + palette colour).
    for (int v = 0; v < kNumChannels; ++v)
    {
        voiceSlots[(size_t) v].name        = getChannelName(v).toStdString();
        voiceSlots[(size_t) v].colourIndex = getChannelColourIndex(v);
    }

    seqSwingParam  = apvts.getRawParameterValue("seq_swing");
    seqAccentParam = apvts.getRawParameterValue("seq_accent");
    grooveVoices.setSlots(&voiceSlots);
    grooveVoices.cacheParams(apvts);

    initAppSettings("muOn");   // settings file + saved UI size / MIDI clock (ProcessorBase)

    initLaneState();        // the lane layout every preset / session save and load uses
    hotSwap.setAppliers([this](juce::ValueTree& t) { applyStateTree(t); },
                        [this](int i, juce::ValueTree& t) { applyTrackTree(i, t); });
    startFxParamSync();     // mixer / FX params → mixerEngine + fxChain (ProcessorBase)
    loadStartupDefault();   // restore a saved _default preset (skipped by render mode)
}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    sequencer.prepare(sampleRate);
    grooveVoices.prepare(sampleRate, samplesPerBlock);
    mixerEngine.prepare(sampleRate, samplesPerBlock);
    fxChain.prepare(sampleRate, samplesPerBlock);
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    // Preserve the DAW sidechain, then clear (shared — the SC input bus shares buffer
    // channels with the output, so a bare clear would wipe it).
    captureSidechainAndClear(buffer);

    // MIDI program change → preset load: queue matching PCs (Ch 1-5 track, Ch 9 full) for
    // handleAsyncUpdate to load on the message thread.
    queueMidiProgramChanges(midiMessages);

    // This block's transport — the family rule (mu-core TransportResolver): the host / mu-link
    // position, else (standalone) external MIDI clock, else the own transport.
    const double midiClockBeat = midiClockSync.process(midiMessages, numSamples, currentSampleRate);
    const auto   transport     = mu_core::resolveTransport(mu_core::readHostTransport(getPlayHead()),
                                                           wrapperType == wrapperType_Standalone,
                                                           midiClockSync, midiClockBeat,
                                                           { internalPlaying, internalBpm, internalBeatPos },
                                                           numSamples, currentSampleRate);
    const double bpm       = transport.bpm;
    const double beatStart = transport.startBeat;
    const bool   isPlaying = transport.playing;

    // Refresh engine params from the APVTS, then clock the 909 sequencer for this block
    // (before the render so a step's engine is armed for this same block). Each fired lane
    // triggers its engine and bumps a counter the editor polls to pulse the sidebar.
    grooveVoices.applyParams(beatStart, bpm);
    // Stop edge: silence the voices so a sustaining bass (no note-off yet) doesn't drone on.
    if (wasPlaying && ! isPlaying) grooveVoices.reset();
    wasPlaying = isPlaying;
    if (isPlaying)
    {
        sequencer.setSwing (seqSwingParam  ? seqSwingParam->load()  : 0.0f);
        sequencer.setAccentVelocity(seqAccentParam ? seqAccentParam->load() : 1.0f);
        sequencer.process(beatStart, numSamples, bpm,
                          [this](int track, float vel, int off)
                          {
                              grooveVoices.trigger(track, vel, off);
                              if (track >= 0 && track < kNumChannels)
                                  triggers[(size_t) track].fetch_add(1, std::memory_order_relaxed);
                          });
    }

    // (External DAW sidechain already captured at the top of processBlock, before the clear.)

    // Render the engines → mixer through the shared path (engine→insert→mixer).
    processCoreBlock(buffer, nullptr, kNumChannels, numSamples, bpm,
                     nullptr, nullptr, nullptr, &renderChannelCb);

    // Hot-swap: flag staged presets whose pattern wrap (one bar) passed in this block, or the
    // transport stopped. When the beat comes from outside, the next block's start stands in for
    // this block's end, so the block's own span is estimated from the tempo.
    if (hotSwap.flagBoundaries(isPlaying, beatStart, transport.beatsPerSample * (double) numSamples))
        triggerAsyncUpdate();
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor(*this);
}

void PluginProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = captureState().createXml())
        copyXmlToBinary(*xml, destData);
}

void PluginProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            applyStateTree(toLaneState(juce::ValueTree::fromXml(*xml)));   // host restore — always immediate
}


juce::File PluginProcessor::getPerSlotPresetDir() const { return getContentDir().getChildFile("Tracks"); }

} // namespace mu_on

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new mu_on::PluginProcessor();
}
