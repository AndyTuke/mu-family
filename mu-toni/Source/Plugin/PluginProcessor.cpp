#include "Plugin/PluginProcessor.h"
#include "Plugin/PluginEditor.h"
#include "Plugin/HostTransport.h"   // mu-core: DAW / mu-link transport read

namespace mu_toni
{

// ── Per-voice arp/voice/env parameter slots ──────────────────────────────────
// Order MUST match the vpSuffix[] table + the layout added in createParameterLayout.
// Read via vp[voice][slot]->load() on the audio thread (pointers cached once).
namespace vpi
{
    enum
    {
        scale, root, roct, chord, inv, octs, dir, rate, gate, leg, porta, snap, trig,     // arp (13)
        o1w, o1o, o1f, o1l, o2w, o2o, o2s, o2f, o2l, pw, noise, ft, cut, res, drv,          // voice (15)
        aeA, aeD, aeS, aeR, aeL, feA, feD, feS, feR, feDep, peA, peD, peS, peR, peDep,      // envs (15)
        COUNT
    };
    static const char* const suffix[COUNT] = {
        "scale","root","roct","chord","inv","octs","dir","rate","gate","leg","porta","snap","trig",
        "o1w","o1o","o1f","o1l","o2w","o2o","o2s","o2f","o2l","pw","noise","ft","cut","res","drv",
        "aeA","aeD","aeS","aeR","aeL","feA","feD","feS","feR","feDep","peA","peD","peS","peR","peDep",
    };
    static_assert(COUNT == 43, "vpi slot count must equal PluginProcessor::kNumVoiceParams");
}

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

    // ── Per-voice arpeggiator + analogue voice + envelopes (v{N}_*) ───────────
    // A time range (seconds) skewed so short attacks/decays get resolution.
    auto tf = []
    {
        NormalisableRange<float> r(0.001f, 4.0f, 0.001f);
        r.setSkewForCentre(0.2f);
        return r;
    };
    NormalisableRange<float> cutR(20.0f, 18000.0f, 1.0f);
    cutR.setSkewForCentre(1200.0f);

    for (int i = 0; i < kNumChannels; ++i)
    {
        const String v = "v" + String(i) + "_";
        auto pid = [&](const char* s) { return ParameterID{ v + s, 1 }; };
        auto lbl = [&](const char* s) { return "V" + String(i + 1) + " " + s; };
        const bool aud = (i == 0);   // only voice 1 audible on a fresh patch

        // Arp
        layout.add(std::make_unique<AudioParameterInt>  (pid("scale"), lbl("Scale"), 0, 11, 1));      // Minor
        layout.add(std::make_unique<AudioParameterInt>  (pid("root"),  lbl("Root"),  0, 11, 0));      // C
        layout.add(std::make_unique<AudioParameterInt>  (pid("roct"),  lbl("Root Octave"), 0, 8, 4));
        layout.add(std::make_unique<AudioParameterInt>  (pid("chord"), lbl("Chord"), 0, 34, 3));      // Minor
        layout.add(std::make_unique<AudioParameterInt>  (pid("inv"),   lbl("Inversion"), -4, 4, 0));
        layout.add(std::make_unique<AudioParameterInt>  (pid("octs"),  lbl("Octaves"), 1, 4, 2));
        layout.add(std::make_unique<AudioParameterFloat>(pid("dir"),   lbl("Direction"), f(-100.0f, 100.0f, 1.0f), 100.0f));
        layout.add(std::make_unique<AudioParameterInt>  (pid("rate"),  lbl("Rate"), 0, 11, 6));       // 1/16
        layout.add(std::make_unique<AudioParameterFloat>(pid("gate"),  lbl("Gate Length"), f(1.0f, 100.0f, 1.0f), 50.0f));
        layout.add(std::make_unique<AudioParameterBool> (pid("leg"),   lbl("Legato"), false));
        layout.add(std::make_unique<AudioParameterFloat>(pid("porta"), lbl("Portamento"), f(0.0f, 500.0f, 1.0f), 0.0f));
        layout.add(std::make_unique<AudioParameterBool> (pid("snap"),  lbl("Diatonic Snap"), false));
        layout.add(std::make_unique<AudioParameterInt>  (pid("trig"),  lbl("Trigger"), 0, 1, 0));     // 0=Loop

        // Oscillators / filter
        layout.add(std::make_unique<AudioParameterInt>  (pid("o1w"), lbl("Osc1 Wave"), 0, 4, 2));     // Saw
        layout.add(std::make_unique<AudioParameterInt>  (pid("o1o"), lbl("Osc1 Octave"), -3, 3, 0));
        layout.add(std::make_unique<AudioParameterFloat>(pid("o1f"), lbl("Osc1 Fine"), f(-100.0f, 100.0f, 1.0f), 0.0f));
        layout.add(std::make_unique<AudioParameterFloat>(pid("o1l"), lbl("Osc1 Level"), f(-60.0f, 6.0f, 0.1f), aud ? 0.0f : -60.0f));
        layout.add(std::make_unique<AudioParameterInt>  (pid("o2w"), lbl("Osc2 Wave"), 0, 4, 2));
        layout.add(std::make_unique<AudioParameterInt>  (pid("o2o"), lbl("Osc2 Octave"), -3, 3, 0));
        layout.add(std::make_unique<AudioParameterInt>  (pid("o2s"), lbl("Osc2 Semi"), -12, 12, 0));
        layout.add(std::make_unique<AudioParameterFloat>(pid("o2f"), lbl("Osc2 Fine"), f(-100.0f, 100.0f, 1.0f), 7.0f));
        layout.add(std::make_unique<AudioParameterFloat>(pid("o2l"), lbl("Osc2 Level"), f(-60.0f, 6.0f, 0.1f), aud ? -3.0f : -60.0f));
        layout.add(std::make_unique<AudioParameterFloat>(pid("pw"),    lbl("Pulse Width"), f(0.05f, 0.95f, 0.001f), 0.5f));
        layout.add(std::make_unique<AudioParameterFloat>(pid("noise"), lbl("Noise Level"), f(-60.0f, 0.0f, 0.1f), -60.0f));
        layout.add(std::make_unique<AudioParameterInt>  (pid("ft"),  lbl("Filter Type"), 0, 15, 0));
        layout.add(std::make_unique<AudioParameterFloat>(pid("cut"), lbl("Cutoff"), cutR, 3000.0f));
        layout.add(std::make_unique<AudioParameterFloat>(pid("res"), lbl("Resonance"), f(0.0f, 0.99f, 0.001f), 0.3f));
        layout.add(std::make_unique<AudioParameterFloat>(pid("drv"), lbl("Drive"), f(0.0f, 1.0f, 0.001f), 0.0f));

        // Amp / Filter / Pitch ADSR
        layout.add(std::make_unique<AudioParameterFloat>(pid("aeA"), lbl("Amp Attack"),  tf(), 0.004f));
        layout.add(std::make_unique<AudioParameterFloat>(pid("aeD"), lbl("Amp Decay"),   tf(), 0.15f));
        layout.add(std::make_unique<AudioParameterFloat>(pid("aeS"), lbl("Amp Sustain"), f(0.0f, 1.0f, 0.001f), 0.5f));
        layout.add(std::make_unique<AudioParameterFloat>(pid("aeR"), lbl("Amp Release"), tf(), 0.15f));
        layout.add(std::make_unique<AudioParameterFloat>(pid("aeL"), lbl("Amp Level"),   f(-60.0f, 6.0f, 0.1f), 0.0f));
        layout.add(std::make_unique<AudioParameterFloat>(pid("feA"), lbl("Filter Attack"),  tf(), 0.004f));
        layout.add(std::make_unique<AudioParameterFloat>(pid("feD"), lbl("Filter Decay"),   tf(), 0.20f));
        layout.add(std::make_unique<AudioParameterFloat>(pid("feS"), lbl("Filter Sustain"), f(0.0f, 1.0f, 0.001f), 0.0f));
        layout.add(std::make_unique<AudioParameterFloat>(pid("feR"), lbl("Filter Release"), tf(), 0.20f));
        layout.add(std::make_unique<AudioParameterFloat>(pid("feDep"), lbl("Filter Env Depth"), f(-1.0f, 1.0f, 0.001f), 0.3f));
        layout.add(std::make_unique<AudioParameterFloat>(pid("peA"), lbl("Pitch Attack"),  tf(), 0.004f));
        layout.add(std::make_unique<AudioParameterFloat>(pid("peD"), lbl("Pitch Decay"),   tf(), 0.10f));
        layout.add(std::make_unique<AudioParameterFloat>(pid("peS"), lbl("Pitch Sustain"), f(0.0f, 1.0f, 0.001f), 0.0f));
        layout.add(std::make_unique<AudioParameterFloat>(pid("peR"), lbl("Pitch Release"), tf(), 0.10f));
        layout.add(std::make_unique<AudioParameterFloat>(pid("peDep"), lbl("Pitch Env Depth"), f(-24.0f, 24.0f, 0.1f), 0.0f));
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
    // Per-channel render: each channel runs its own arpeggiator voice into the
    // buffer; the shared mixer applies the strip + master mix downstream.
    renderChannelCb = [this](int ch, juce::AudioBuffer<float>& buf, int n)
    {
        if (ch >= 0 && ch < kMaxChannels) runners[(size_t) ch].render(buf, n, arpCtx);
        else                              buf.clear();
    };

    // Persistent settings file (UI scale + MIDI-clock prefs) — mirrors mu-tant.
    {
        juce::PropertiesFile::Options opts;
        opts.applicationName     = "muToni";
        opts.filenameSuffix      = "xml";
        opts.folderName          = "TDP";
        opts.osxLibrarySubFolder = "Application Support";
        auto settingsFile = opts.getDefaultFile();
        settingsFile.getParentDirectory().createDirectory();
        appSettings = std::make_unique<juce::PropertiesFile>(settingsFile, opts);
    }
    uiScale = juce::jlimit(kUiScaleMedium, kUiScaleLarge,
                           (float) appSettings->getDoubleValue("uiScale", (double) kUiScaleMedium));
    // Restore persisted MIDI-clock-sync prefs (standalone external clock).
    midiClockSync.setEnabled (appSettings->getBoolValue("midiSyncEnabled",  false));
    midiClockSync.setMessages(appSettings->getIntValue ("midiSyncMessages", 2));

    registerFxListeners();
    syncAllFxParams();   // JUCE doesn't fire parameterChanged on construction
    cacheVoiceParamPointers();
}

void PluginProcessor::cacheVoiceParamPointers()
{
    for (int i = 0; i < kMaxChannels; ++i)
        for (int k = 0; k < kNumVoiceParams; ++k)
        {
            const juce::String id = "v" + juce::String(i) + "_" + vpi::suffix[k];
            vp[(size_t) i][(size_t) k] = (i < kNumChannels) ? apvts.getRawParameterValue(id) : nullptr;
        }
}

void PluginProcessor::readVoice(int v, ArpParams& ap, ToniVoiceParams& tv,
                                int& rateIdx, float& gate01, bool& midiTrig) const
{
    const auto& p = vp[(size_t) v];
    auto g = [&](int slot) { return p[(size_t) slot] != nullptr ? p[(size_t) slot]->load() : 0.0f; };

    ap.scale        = (int) g(vpi::scale);
    ap.rootNote     = (int) g(vpi::root);
    ap.rootOctave   = (int) g(vpi::roct);
    ap.chord        = (int) g(vpi::chord);
    ap.inversion    = (int) g(vpi::inv);
    ap.octavesSpan  = (int) g(vpi::octs);
    ap.direction    = g(vpi::dir);
    ap.diatonicSnap = g(vpi::snap) > 0.5f;

    tv.osc1Shape = (int) g(vpi::o1w); tv.osc1Oct = (int) g(vpi::o1o); tv.osc1Fine = g(vpi::o1f); tv.osc1LevelDb = g(vpi::o1l);
    tv.osc2Shape = (int) g(vpi::o2w); tv.osc2Oct = (int) g(vpi::o2o); tv.osc2Semi = g(vpi::o2s); tv.osc2Fine = g(vpi::o2f); tv.osc2LevelDb = g(vpi::o2l);
    tv.pulseWidth = g(vpi::pw); tv.noiseLevelDb = g(vpi::noise);
    tv.filterType = (int) g(vpi::ft); tv.cutoff = g(vpi::cut); tv.resonance = g(vpi::res); tv.drive = g(vpi::drv);

    tv.ampA = g(vpi::aeA); tv.ampD = g(vpi::aeD); tv.ampS = g(vpi::aeS); tv.ampR = g(vpi::aeR); tv.ampLevelDb = g(vpi::aeL);
    tv.fA = g(vpi::feA); tv.fD = g(vpi::feD); tv.fS = g(vpi::feS); tv.fR = g(vpi::feR); tv.filterEnvDepth = g(vpi::feDep);
    tv.pA = g(vpi::peA); tv.pD = g(vpi::peD); tv.pS = g(vpi::peS); tv.pR = g(vpi::peR); tv.pitchEnvDepth = g(vpi::peDep);

    tv.portamentoMs = g(vpi::porta);
    tv.legato       = g(vpi::leg) > 0.5f;
    tv.pan          = 0.0f;   // pan handled by the mixer strip

    rateIdx  = (int) g(vpi::rate);
    gate01   = g(vpi::gate) * 0.01f;
    midiTrig = g(vpi::trig) > 0.5f;
}

void PluginProcessor::updateHeldNotes(const juce::MidiBuffer& midi, bool& noteOnEdge)
{
    noteOnEdge = false;
    auto removeNote = [this](int note)
    {
        for (int i = 0; i < heldCount; ++i)
            if (heldStack[(size_t) i] == note)
            {
                for (int j = i; j < heldCount - 1; ++j) heldStack[(size_t) j] = heldStack[(size_t) j + 1];
                --heldCount;
                return;
            }
    };

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())
        {
            removeNote(m.getNoteNumber());
            if (heldCount < (int) heldStack.size()) heldStack[(size_t) heldCount++] = m.getNoteNumber();
            noteOnEdge = true;
        }
        else if (m.isNoteOff())          removeNote(m.getNoteNumber());
        else if (m.isAllNotesOff() || m.isAllSoundOff()) heldCount = 0;
    }
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
    for (int i = 0; i < kNumChannels; ++i)
        runners[(size_t) i].prepare(sampleRate, samplesPerBlock);
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

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    // Preserve the DAW sidechain, then clear (shared — the SC input bus shares buffer
    // channels with the output, so a bare clear would wipe it).
    captureSidechainAndClear(buffer);

    // External MIDI clock (standalone): scan the buffer + advance the clock estimate.
    // When enabled + playing it drives the tempo + play-state (transport bar reflects it).
    midiClockSync.process(midiMessages, numSamples, currentSampleRate);
    const bool clockSlaved = wrapperType == wrapperType_Standalone
                             && midiClockSync.isEnabled() && midiClockSync.isPlaying();
    // Transport priority: DAW host / mu-link master (via injected playhead) > external
    // MIDI clock (standalone) > the internal free-running transport.
    const auto host = mu_core::readHostTransport(getPlayHead());
    double bpm;
    bool   isPlaying;
    if (host.hasPosition)
    {
        isPlaying = host.playing;
        bpm       = host.bpm > 0.0 ? host.bpm : internalBpm.load(std::memory_order_relaxed);
    }
    else if (clockSlaved)
    {
        bpm       = midiClockSync.getBpm();
        isPlaying = true;
        playing.store(true, std::memory_order_relaxed);   // UI play button reflects the clock
    }
    else
    {
        bpm       = internalBpm.load(std::memory_order_relaxed);
        isPlaying = playing.load(std::memory_order_relaxed);
    }

    // Root-by-MIDI / trigger: update the held-note stack from incoming notes.
    bool noteOnEdge = false;
    updateHeldNotes(midiMessages, noteOnEdge);

    // Per-block arp context (read by the render callback for every voice).
    arpCtx.playing          = isPlaying;
    arpCtx.sampleRate       = currentSampleRate;
    arpCtx.bpm              = bpm;
    arpCtx.anyNoteHeld      = heldCount > 0;
    arpCtx.rootOverrideMidi = heldCount > 0 ? heldStack[(size_t) (heldCount - 1)] : -1;
    arpCtx.noteOnEdge       = noteOnEdge;

    // Push current parameters into each voice's arp runner.
    for (int i = 0; i < kNumChannels; ++i)
    {
        ArpParams ap; ToniVoiceParams tv; int rateIdx = 6; float gate01 = 0.5f; bool midiTrig = false;
        readVoice(i, ap, tv, rateIdx, gate01, midiTrig);
        runners[(size_t) i].setArp(ap);
        runners[(size_t) i].setVoiceParams(tv);
        runners[(size_t) i].setStep(rateIdx, gate01, midiTrig);
    }

    // Render each arp voice → mixer through the shared path (engine → insert →
    // mixer): the render hook fills each channel, the mixer owns strip + master.
    processCoreBlock(buffer, nullptr, kNumChannels, numSamples, bpm,
                     nullptr, nullptr, nullptr, &renderChannelCb);

    // Advance the free-running transport while playing (drives the beat-pos UI).
    if (isPlaying)
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
juce::File PluginProcessor::getPerSlotPresetDir() const { return getContentDir().getChildFile("Arps"); }

void PluginProcessor::setUiScale(float scale)
{
    ProcessorBase::setUiScale(scale);   // clamps + notifies the editor
    if (appSettings != nullptr) { appSettings->setValue("uiScale", (double) getUiScale()); appSettings->saveIfNeeded(); }
}

void PluginProcessor::setMidiSyncEnabled(bool on)
{
    midiClockSync.setEnabled(on);
    if (appSettings != nullptr) { appSettings->setValue("midiSyncEnabled", on); appSettings->saveIfNeeded(); }
}

void PluginProcessor::setMidiSyncMessages(int mode)
{
    midiClockSync.setMessages(mode);
    if (appSettings != nullptr) { appSettings->setValue("midiSyncMessages", mode); appSettings->saveIfNeeded(); }
}

} // namespace mu_toni

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new mu_toni::PluginProcessor();
}
