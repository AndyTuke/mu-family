#include "Plugin/PluginProcessor.h"
#include "Plugin/PluginEditor.h"
#include "Plugin/HostTransport.h"          // mu-core: DAW / mu-link transport read
#include "Modulation/ModulatorSerialise.h" // mu-core: modulator state save/load

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
        o1wt, o1o, o1f, o1pos, o1l, o2wt, o2o, o2s, o2f, o2pos, o2l,                            // oscillators (11)
        xmPh, xmIdx, sync, xmFb, xmAm, xmDep, xmSsb,                                              // cross-mod (7)
        noise, ntype, ft, cut, res, drv, locut,                                                  // noise + filter (7)
        aeA, aeD, aeS, aeR, aeL, feA, feD, feS, feR, feDep, peA, peD, peS, peR, peDep, ptgt,      // envs (16)
        drvChar, insP1, insP2, insP3, insP4,                                                     // insert (5)
        COUNT
    };
    static const char* const suffix[COUNT] = {
        "scale","root","roct","chord","inv","octs","dir","rate","gate","leg","porta","snap","trig",
        "o1_wt","o1o","o1f","o1_pos","o1l","o2_wt","o2o","o2s","o2f","o2_pos","o2l",
        "xmod_phaseMode","xmod_index","sync","xmod_fdbk","xmod_ampMode","xmod_depth","xmod_ssb",
        "noise","ntype","ft","cut","res","drv","locut",
        "aeA","aeD","aeS","aeR","aeL","feA","feD","feS","feR","feDep","peA","peD","peS","peR","peDep","ptgt",
        "drvChar","insP1","insP2","insP3","insP4",
    };
    static_assert(COUNT == 59, "vpi slot count must equal PluginProcessor::kNumVoiceParams");
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
        mu_mixfx::addChannelStripParams(layout, c, n);
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

        // Oscillators — mu-Tant's wavetable oscs: a table from the shared bank + its scan
        // position (frame 0..255), defaulting to Basic Shapes' saw.
        const int maxWt = juce::jmax(0, mu_wavetable::WavetableBank::factoryTableNames().size() - 1);
        layout.add(std::make_unique<AudioParameterInt>  (pid("o1_wt"),  lbl("Osc1 Wavetable"), 0, maxWt, 0));
        layout.add(std::make_unique<AudioParameterInt>  (pid("o1o"),    lbl("Osc1 Octave"), -3, 3, 0));
        layout.add(std::make_unique<AudioParameterFloat>(pid("o1f"),    lbl("Osc1 Fine"), f(-100.0f, 100.0f, 1.0f), 0.0f));
        layout.add(std::make_unique<AudioParameterInt>  (pid("o1_pos"), lbl("Osc1 Position"), 0, 255, kSawPosition));
        layout.add(std::make_unique<AudioParameterFloat>(pid("o1l"),    lbl("Osc1 Level"), f(-60.0f, 6.0f, 0.1f), aud ? 0.0f : -60.0f));
        layout.add(std::make_unique<AudioParameterInt>  (pid("o2_wt"),  lbl("Osc2 Wavetable"), 0, maxWt, 0));
        layout.add(std::make_unique<AudioParameterInt>  (pid("o2o"),    lbl("Osc2 Octave"), -3, 3, 0));
        layout.add(std::make_unique<AudioParameterInt>  (pid("o2s"),    lbl("Osc2 Semi"), -12, 12, 0));
        layout.add(std::make_unique<AudioParameterFloat>(pid("o2f"),    lbl("Osc2 Fine"), f(-100.0f, 100.0f, 1.0f), 7.0f));
        layout.add(std::make_unique<AudioParameterInt>  (pid("o2_pos"), lbl("Osc2 Position"), 0, 255, kSawPosition));
        layout.add(std::make_unique<AudioParameterFloat>(pid("o2l"),    lbl("Osc2 Level"), f(-60.0f, 6.0f, 0.1f), aud ? -3.0f : -60.0f));

        // Cross-mod — mu-Tant's 2-lane model (same ids + ranges): Lane A index + mode + Sync +
        // Feedback; Lane B bipolar depth + mode, with the SSB shift in Hz.
        layout.add(std::make_unique<AudioParameterChoice>(pid("xmod_phaseMode"), lbl("X-Mod Phase Mode"), StringArray{ "FM", "PM", "TZFM" }, 1));
        layout.add(std::make_unique<AudioParameterFloat> (pid("xmod_index"),     lbl("X-Mod Index"), f(0.0f, 100.0f, 1.0f), 0.0f));
        layout.add(std::make_unique<AudioParameterBool>  (pid("sync"),           lbl("Osc Sync"), false));
        layout.add(std::make_unique<AudioParameterBool>  (pid("xmod_fdbk"),      lbl("X-Mod Feedback"), false));
        layout.add(std::make_unique<AudioParameterChoice>(pid("xmod_ampMode"),   lbl("X-Mod Amp Mode"), StringArray{ "AM", "RM", "SSB" }, 0));
        layout.add(std::make_unique<AudioParameterFloat> (pid("xmod_depth"),     lbl("X-Mod Depth"), f(-100.0f, 100.0f, 1.0f), 0.0f));
        layout.add(std::make_unique<AudioParameterFloat> (pid("xmod_ssb"),       lbl("X-Mod SSB Shift"), f(-2000.0f, 2000.0f, 1.0f), 0.0f,
                    AudioParameterFloatAttributes().withStringFromValueFunction(
                        [](float v, int) -> juce::String { return juce::String((int) std::round(v)) + " Hz"; })));

        // Noise / filter
        layout.add(std::make_unique<AudioParameterFloat>(pid("noise"), lbl("Noise Level"), f(-60.0f, 0.0f, 0.1f), -60.0f));
        layout.add(std::make_unique<AudioParameterInt>  (pid("ntype"), lbl("Noise Type"), 0, 1, 0));    // 0=White
        layout.add(std::make_unique<AudioParameterInt>  (pid("ft"),  lbl("Filter Type"), 0, 15, 0));
        layout.add(std::make_unique<AudioParameterFloat>(pid("cut"), lbl("Cutoff"), cutR, 3000.0f));
        layout.add(std::make_unique<AudioParameterFloat>(pid("res"), lbl("Resonance"), f(0.0f, 0.99f, 0.001f), 0.3f));
        layout.add(std::make_unique<AudioParameterFloat>(pid("drv"), lbl("Drive"), f(0.0f, 1.0f, 0.001f), 0.0f));
        { NormalisableRange<float> loR(0.0f, 1000.0f, 1.0f); loR.setSkewForCentre(200.0f);
          layout.add(std::make_unique<AudioParameterFloat>(pid("locut"), lbl("Low Cut"), loR, 0.0f)); }

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
        layout.add(std::make_unique<AudioParameterInt>  (pid("ptgt"),  lbl("Pitch Env Target"), 0, 1, 1));   // 1=Osc 2 only

        // Insert effect (shared mu-core InsertProcessor) — same schema as mu-clid/mu-tant.
        layout.add(std::make_unique<AudioParameterInt>  (pid("drvChar"), lbl("Insert Algo"), 0, InsertProcessor::kNumInsertAlgos - 1, 0));
        layout.add(std::make_unique<AudioParameterFloat>(pid("insP1"), lbl("Insert P1"), f(0.0f, 1.0f, 0.0f), 0.0f));
        layout.add(std::make_unique<AudioParameterFloat>(pid("insP2"), lbl("Insert P2"), f(0.0f, 1.0f, 0.0f), 0.0f));
        layout.add(std::make_unique<AudioParameterFloat>(pid("insP3"), lbl("Insert P3"), f(0.0f, 1.0f, 0.0f), 0.0f));
        layout.add(std::make_unique<AudioParameterFloat>(pid("insP4"), lbl("Insert P4"), f(0.0f, 1.0f, 0.0f), 0.0f));
    }

    // ── MIDI in: which channel's notes drive the arps (Omni = any) ─────────────
    {
        StringArray chans { "Omni" };
        for (int c = 1; c <= 16; ++c) chans.add(String(c));
        layout.add(std::make_unique<AudioParameterChoice>(ParameterID{ "midiInCh", 1 }, "MIDI In Channel", chans, 0));
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
        if (ch >= 0 && ch < kMaxChannels)
        {
            runners[(size_t) ch].render(buf, n, arpCtx);
            inserts[(size_t) ch].process(buf, n, buf.getNumChannels(), insCfg[(size_t) ch]);   // engine → insert → mixer
        }
        else buf.clear();
    };

    initAppSettings("muToni");   // settings file + saved UI size / MIDI clock (ProcessorBase)

    // The shared wavetable bank (procedural factory set), read by every voice's oscillators.
    bank.loadFactoryBank();
    for (auto& r : runners) r.setBank(&bank);
    midiInChParam = apvts.getRawParameterValue("midiInCh");

    cacheVoiceParamPointers();
    initLayerState();       // the layer layout every preset / session save and load uses
    hotSwap.setAppliers([this](juce::ValueTree& t) { applyStateTree(t); },
                        [this](int i, juce::ValueTree& t) { applyLayerTree(i, t); });
    startFxParamSync();     // mixer / FX params → mixerEngine + fxChain (ProcessorBase)
    loadStartupDefault();   // restore a saved _default preset (skipped by render mode)
}

namespace
{
    // The per-voice slot (vpi::*) a modulation target drives, found from its table param name.
    int vpSlotFor(const char* param)
    {
        for (int k = 0; k < vpi::COUNT; ++k)
            if (std::strcmp(vpi::suffix[k], param) == 0) return k;
        jassertfalse;   // a table row names a parameter that does not exist
        return 0;
    }
}

void PluginProcessor::cacheVoiceParamPointers()
{
    for (int i = 0; i < kMaxChannels; ++i)
        for (int k = 0; k < kNumVoiceParams; ++k)
        {
            const juce::String id = "v" + juce::String(i) + "_" + vpi::suffix[k];
            vp[(size_t) i][(size_t) k] = (i < kNumChannels) ? apvts.getRawParameterValue(id) : nullptr;
        }

    // Modulation-resolve inputs: ids + ranges (voice-independent) + per-voice atoms.
    for (int k = 0; k < kNumModDests; ++k)
    {
        modDestIds[(size_t) k]    = kModDestTable[k].id;
        modDestRanges[(size_t) k] = apvts.getParameterRange("v0_" + juce::String(kModDestTable[k].param));
        for (int i = 0; i < kMaxChannels; ++i)
            modDestAtoms[(size_t) i][(size_t) k] = (i < kNumChannels) ? vp[(size_t) i][(size_t) vpSlotFor(kModDestTable[k].param)]
                                                                      : nullptr;
        modParamValues[kModDestTable[k].id] = 0.0f;   // pre-size the map (no audio-thread alloc)
    }
}

void PluginProcessor::readVoice(int v, ArpParams& ap, ToniVoiceParams& tv,
                                int& rateIdx, float& gate01, bool& midiTrig)
{
    const auto& p = vp[(size_t) v];
    auto g = [&](int slot) { return p[(size_t) slot] != nullptr ? p[(size_t) slot]->load() : 0.0f; };

    // Resolve this voice's modulation matrix over its control sequences → out[] (param units).
    // Modulated destinations come from out[]; everything else reads the raw param.
    float out[kNumModDests];
    mu_mod::resolveLane(&voiceSlots[(size_t) v], modBeat, kNumModDests,
                        modDestIds.data(), modDestAtoms[(size_t) v].data(),
                        modDestRanges.data(), modParamValues, out);

    // Arp (integer dests round; direction/gate/porta continuous).
    ap.scale        = (int) g(vpi::scale);                  // not modulated
    ap.rootNote     = juce::roundToInt(out[D_root]);
    ap.rootOctave   = juce::roundToInt(out[D_roct]);
    ap.chord        = juce::roundToInt(out[D_chord]);
    ap.inversion    = juce::roundToInt(out[D_inv]);
    ap.octavesSpan  = juce::roundToInt(out[D_octs]);
    ap.direction    = out[D_dir];
    ap.diatonicSnap = g(vpi::snap) > 0.5f;                  // not modulated

    tv.osc1Table = (int) g(vpi::o1wt); tv.osc1Oct = (int) g(vpi::o1o); tv.osc1Fine = g(vpi::o1f); tv.osc1Pos = out[D_o1pos]; tv.osc1LevelDb = out[D_o1lvl];
    tv.osc2Table = (int) g(vpi::o2wt); tv.osc2Oct = (int) g(vpi::o2o); tv.osc2Semi = out[D_o2semi]; tv.osc2Fine = g(vpi::o2f);
    tv.osc2Pos   = out[D_o2pos]; tv.osc2LevelDb = out[D_o2lvl];
    tv.noiseLevelDb = out[D_noise]; tv.noiseType = (int) g(vpi::ntype);

    // Cross-mod (index / depth are 0..100 / ±100 knobs → 0..1 / ±1).
    tv.xmod.phaseMode = (int) g(vpi::xmPh);
    tv.xmod.index     = out[D_xmIdx] * 0.01f;
    tv.xmod.sync      = g(vpi::sync) > 0.5f;
    tv.xmod.feedback  = g(vpi::xmFb) > 0.5f;
    tv.xmod.ampMode   = (int) g(vpi::xmAm);
    tv.xmod.depth     = out[D_xmDep] * 0.01f;
    tv.xmod.ssbHz     = out[D_xmSsb];
    tv.filterType = (int) g(vpi::ft); tv.cutoff = out[D_cut]; tv.resonance = out[D_res]; tv.drive = out[D_drv]; tv.lowCutHz = g(vpi::locut);

    tv.ampA = g(vpi::aeA); tv.ampD = g(vpi::aeD); tv.ampS = g(vpi::aeS); tv.ampR = g(vpi::aeR); tv.ampLevelDb = out[D_amp];
    tv.fA = g(vpi::feA); tv.fD = g(vpi::feD); tv.fS = g(vpi::feS); tv.fR = g(vpi::feR); tv.filterEnvDepth = out[D_fenv];
    tv.pA = g(vpi::peA); tv.pD = g(vpi::peD); tv.pS = g(vpi::peS); tv.pR = g(vpi::peR); tv.pitchEnvDepth = out[D_penv];
    tv.pitchEnvTarget = (int) g(vpi::ptgt);

    tv.portamentoMs = out[D_porta];
    tv.legato       = g(vpi::leg) > 0.5f;
    tv.pan          = 0.0f;   // pan handled by the mixer strip

    rateIdx  = juce::roundToInt(out[D_rate]);
    gate01   = out[D_gate] * 0.01f;
    midiTrig = g(vpi::trig) > 0.5f;

    // Insert config (applied post-VCA in the render callback).
    auto& ic = insCfg[(size_t) v];
    ic.insertAlgo     = (int) g(vpi::drvChar);
    ic.insertParam[0] = g(vpi::insP1);
    ic.insertParam[1] = g(vpi::insP2);
    ic.insertParam[2] = g(vpi::insP3);
    ic.insertParam[3] = g(vpi::insP4);
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

    // MIDI In channel: Omni takes every channel's notes, else only that channel's.
    const int inCh = midiInChParam != nullptr ? juce::roundToInt(midiInChParam->load()) : 0;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (inCh > 0 && m.getChannel() != inCh) continue;
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

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    mixerEngine.prepare(sampleRate, samplesPerBlock);
    fxChain.prepare(sampleRate, samplesPerBlock);
    for (int i = 0; i < kNumChannels; ++i)
    {
        runners[(size_t) i].prepare(sampleRate, samplesPerBlock);
        inserts[(size_t) i].prepare(sampleRate, samplesPerBlock);
    }
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    // Preserve the DAW sidechain, then clear (shared — the SC input bus shares buffer
    // channels with the output, so a bare clear would wipe it).
    captureSidechainAndClear(buffer);

    // MIDI program change → preset load: queue matching PCs (Ch 1-4 layer, Ch 9 full) for
    // handleAsyncUpdate to load on the message thread.
    queueMidiProgramChanges(midiMessages);

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
        internalPlaying.store(true, std::memory_order_relaxed);   // UI play button reflects the clock
    }
    else
    {
        bpm       = internalBpm.load(std::memory_order_relaxed);
        isPlaying = internalPlaying.load(std::memory_order_relaxed);
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

    // Beat position for the modulation matrix (control-sequence playhead).
    modBeat = host.hasPosition ? host.ppqPosition
                               : internalBeatPos.load(std::memory_order_relaxed);
    const double blockStartBeat = modBeat;   // the hot-swap bar-line test below

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
    const double blockBeats = (bpm / 60.0) / currentSampleRate * (double) numSamples;
    if (isPlaying)
        internalBeatPos.store(internalBeatPos.load(std::memory_order_relaxed) + blockBeats,
                              std::memory_order_relaxed);

    // Hot-swap: flag staged presets whose bar line passed in this block (or the transport stopped).
    if (hotSwap.flagBoundaries(isPlaying, blockStartBeat, blockBeats))
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
            applyStateTree(toLayerState(juce::ValueTree::fromXml(*xml)));   // host restore — always immediate
}

} // namespace mu_toni

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new mu_toni::PluginProcessor();
}
