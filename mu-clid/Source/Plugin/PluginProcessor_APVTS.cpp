// partial-class TU split from PluginProcessor.cpp. Contains:
// - createParameterLayout (the big APVTS layout factory)
// - parameterChanged dispatcher
// - syncRhythmParam / forceSyncRhythmFromAPVTS (FX/mixer route to mu-core's
//   ProcessorBase::syncGlobalFxParam — see the backlog)
// - pushRhythmToAPVTS / pushMixerChannelToAPVTS / swapAPVTSForRhythms
//
// Shared helpers (kChannelSuffixes, applyRhythmSuffix, adsrTime, adsrSus)
// live in PluginProcessor_Internal.h since the Preset TU needs them too.
// Global param IDs live in kGlobalParamDefs (PresetHelpers.h).

#include "PluginProcessor.h"
#include "PluginProcessor_Internal.h"
#include "Audio/FX/Slots/FXAlgorithmDef.h"
#include "Plugin/MixerFxParams.h"

using mu_pp::kRhythmParamDefs;
using mu_pp::kRhythmParamCount;
using mu_pp::applyRhythmSuffix;

juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto addF = [&](const juce::String& id, const juce::String& name, float mn, float mx, float def)
    {
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            id, name, juce::NormalisableRange<float>(mn, mx), def));
    };
    auto addB = [&](const juce::String& id, const juce::String& name, bool def)
    {
        layout.add(std::make_unique<juce::AudioParameterBool>(id, name, def));
    };
    auto addI = [&](const juce::String& id, const juce::String& name, int mn, int mx, int def)
    {
        layout.add(std::make_unique<juce::AudioParameterInt>(id, name, mn, mx, def));
    };

    // ADSR time params in 0..10 seconds with 0.3 skew. Skew 0.3 puts
    // ~100–200 ms at slider centre so the drum-snap region (1–30 ms) gets
    // generous resolution in the lower third, while pad/ambient values
    // (1–10 s) sit comfortably at the top. Stored value is in seconds
    // directly so host automation lanes show "0.150 s" / "2.5 s" instead
    // of the legacy meaningless 0–100. Format string mirrors the slider's
    // "X ms" below 1 s / "X.XX s" above. Rolled out per envelope to keep
    // each step independently revertable (a → amp, then filter, then pitch).
    auto addAdsrT = [&](const juce::String& id, const juce::String& name, float def)
    {
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            id, name, juce::NormalisableRange<float>(0.0f, 10.0f, 0.0f, 0.3f), def,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction(
                [](float v, int) -> juce::String {
                    if (v >= 1.0f) return juce::String(v, 2) + " s";
                    return juce::String((int)std::round(v * 1000.0f)) + " ms";
                })));
    };

    // ── Per-rhythm parameters (47 × 8 = 376) ─────────────────────────────────
    // Rhythms 0..kAutomatedRhythms-1 use full "Rhythm N " names so DAW automation
    // lanes show them clearly. Remaining rhythms use short "RN " names.
    for (int i = 0; i < SequencerEngine::MaxRhythms; ++i)
    {
        const juce::String p = "r" + juce::String(i) + "_";
        const juce::String n = (i < kAutomatedRhythms)
                                   ? "Rhythm " + juce::String(i + 1) + " "
                                   : "R"       + juce::String(i + 1) + " ";

        // HitGen A
        addI(p+"stepsA",   n+"Steps A",   1, 64, 8);
        addI(p+"hitsA",    n+"Hits A",    0, 64, 0);
        addI(p+"rotA",     n+"Rot A",   -32, 32, 0);
        addI(p+"prePadA",  n+"PrePad A",  0, 12, 0);
        addI(p+"postPadA", n+"PostPad A", 0, 12, 0);
        addI(p+"insStA",   n+"InsSt A",   0, 63, 0);
        addI(p+"insLenA",  n+"InsLen A",  0,  8, 0);
        addB(p+"insModeA",    n+"InsMode A",     false);
        addB(p+"prePadModeA", n+"PrePadMode A",  false);
        addB(p+"postPadModeA",n+"PostPadMode A", false);
        // HitGen B
        addI(p+"stepsB",   n+"Steps B",   1, 64, 8);
        addI(p+"hitsB",    n+"Hits B",    0, 64, 0);
        addI(p+"rotB",     n+"Rot B",   -32, 32, 0);
        addI(p+"prePadB",  n+"PrePad B",  0, 12, 0);
        addI(p+"postPadB", n+"PostPad B", 0, 12, 0);
        addI(p+"insStB",   n+"InsSt B",   0, 63, 0);
        addI(p+"insLenB",  n+"InsLen B",  0,  8, 0);
        addB(p+"insModeB",    n+"InsMode B",     false);
        addB(p+"prePadModeB", n+"PrePadMode B",  false);
        addB(p+"postPadModeB",n+"PostPadMode B", false);
        // HitGen C
        addI(p+"stepsC",   n+"Steps C",   1, 64, 8);
        addI(p+"hitsC",    n+"Hits C",    0, 64, 0);
        addI(p+"rotC",     n+"Rot C",   -32, 32, 0);
        addI(p+"prePadC",  n+"PrePad C",  0, 12, 0);
        addI(p+"postPadC", n+"PostPad C", 0, 12, 0);
        addI(p+"insStC",   n+"InsSt C",   0, 63, 0);
        addI(p+"insLenC",  n+"InsLen C",  0,  8, 0);
        addB(p+"insModeC",    n+"InsMode C",     false);
        addB(p+"prePadModeC", n+"PrePadMode C",  false);
        addB(p+"postPadModeC",n+"PostPadMode C", false);
        // Logic
        addI(p+"logic",  n+"Logic",        0,   4,   0);
        addB(p+"patLeg", n+"Pattern Legato", false);
        addB(p+"vMono",  n+"Voice Mono", false);  // true = polyphony capped at 1 voice
        addI(p+"rstSt",  n+"Reset Steps", -1, 256, -1);  // -1 = free-running (nullopt)
        // Pitch — octave ±3, semi ±12 (±1 oct), fine ±100 cents (1 cent step).
        // Combined static max = ±4 octaves; clamped at the engine.
        addI(p+"pitchOct",  n+"Pitch Oct",  -3,   3,  0);
        addI(p+"pitchSemi", n+"Pitch Semi", -12,  12,  0);
        addI(p+"pitchFine", n+"Pitch Fine", -100, 100, 0);
        addAdsrT(p+"pEnvAtk", n+"P Env Atk", 0.0f);    // seconds (≈ legacy 0.0 × 0.03)
        addAdsrT(p+"pEnvDec", n+"P Env Dec", 0.03f);   //         (≈ legacy 1.0 × 0.03)
        addF     (p+"pEnvSus", n+"P Env Sus", 0.0f, 100.0f, 0.0f);   // sustain stays 0..100 %
        addAdsrT(p+"pEnvRel", n+"P Env Rel", 0.03f);   //         (≈ legacy 1.0 × 0.03)
        addF(p+"pEnvDep",   n+"P Env Dep",  0.0f,  24.0f,  0.0f);
        // Filter
        addI(p+"fltType", n+"Filter Type", 0, 15, 0);  // 0-15: LP12/HP12/BP12/Notch/LP24/HP24/BP24/LP6/Comb+/AP12/Notch24/HP6/Peak/LoShf/HiShf/Comb-
        // log-skewed range. Skew 0.25 puts ~1.3 kHz at slider centre and
        // gives the sub-bass / midrange the resolution they need. Without this,
        // 20–200 Hz lived in ~1% of knob travel.
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            p+"fltCut", n+"Filter Cut",
            juce::NormalisableRange<float>(20.0f, 20000.0f, 0.0f, 0.25f), 8000.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction(
                [](float v, int) -> juce::String {
                    if (v < 1000.0f) return juce::String(v, 1) + " Hz";
                    return juce::String(v / 1000.0f, 1) + " kHz";
                })));
        addF(p+"fltRes",  n+"Filter Res",   0.0f,    0.99f,    0.2f);
        addAdsrT(p+"fEnvAtk", n+"F Env Atk", 0.03f);   // seconds (≈ legacy 1.0 × 0.03)
        addAdsrT(p+"fEnvDec", n+"F Env Dec", 0.09f);   //         (≈ legacy 3.0 × 0.03)
        addF     (p+"fEnvSus", n+"F Env Sus",  0.0f, 100.0f,  0.0f);   // sustain stays 0..100 %
        addAdsrT(p+"fEnvRel", n+"F Env Rel", 0.09f);   //         (≈ legacy 3.0 × 0.03)
        addF(p+"fEnvDep", n+"F Env Dep",  0.0f,  48.0f,  0.0f);
        // 4-pole high-pass that sits inline with the main filter. Skewed so most
        // of the knob travel lives in the audible low-end region (0–200 Hz).
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            p+"fltLoCut", n+"Filter Low Cut",
            juce::NormalisableRange<float>(0.0f, 1000.0f, 0.0f, 0.35f), 0.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction(
                [](float v, int) -> juce::String {
                    if (v <= 0.0f)     return juce::String("Off");
                    if (v < 1000.0f)   return juce::String((int)std::round(v)) + " Hz";
                    return juce::String(v / 1000.0f, 2) + " kHz";
                })));
        // Pre-filter valve saturation depth. 0 = bypass, 1 = full warmth.
        addF(p+"fltDrv", n+"Filter Drive", 0.0f, 1.0f, 0.0f);
        // Amp — level stored in dB (-60..+6), engine converts to gain at read.
        // Default 0 dB = unity gain.
        addF(p+"ampLvl",  n+"Amp Level", -60.0f,   6.0f,  0.0f);
        addAdsrT(p+"aEnvAtk", n+"A Env Atk", 0.005f);   // seconds
        addAdsrT(p+"aEnvDec", n+"A Env Dec", 0.3f);
        addF(p+"aEnvSus", n+"A Env Sus",  0.0f, 100.0f, 80.0f);   // sustain stays 0..100 %
        addAdsrT(p+"aEnvRel", n+"A Env Rel", 0.5f);
        addF(p+"accentDb",  n+"Accent",     0.0f,  12.0f,  0.0f);
        // Insert effect: algo selector + 4 generic Param slots stored as 0..1
        // normalised. Each algorithm's process() reads p.insertParam[N] and
        // converts to the actual value through mu_ui::normToActual using the
        // per-slot range / skew table in Source/Audio/InsertSlotConfig.h. DAW
        // automation lanes show 0..1; the editor UI shows the actual value
        // with the correct unit ("Drive %" / "Bits" / "Mid Hz" / "Note" / ...)
        // based on the active algorithm.
        addI(p+"drvChar", n+"Insert Algo", 0, mu_audio::kInsertAlgorithmCount - 1, 0);
        addF(p+"insP1",   n+"Insert P1",   0.0f, 1.0f, 0.0f);
        addF(p+"insP2",   n+"Insert P2",   0.0f, 1.0f, 0.0f);
        addF(p+"insP3",   n+"Insert P3",   0.0f, 1.0f, 0.0f);
        addF(p+"insP4",   n+"Insert P4",   0.0f, 1.0f, 0.0f);
    }

    // ── Shared global FX + return + master layout (mu-core) ───────────────────
    // Declares eff_/dly_/rev_/eff2*/echo_/ret_*/mstr_lvl/mstr_pan/mst_ins* with
    // the family's reference defaults; synced to fxChain/mixerEngine via
    // ProcessorBase::syncGlobalFxParam. mu-clid keeps only its product-specific
    // globals below (the ch{i}_ rhythm strips + mstrLoop).
    mu_mixfx::addGlobalFxParams(layout);

    // ── Rhythm channel strips (11 × 8 = 88) ──────────────────────────────────
    for (int i = 0; i < SequencerEngine::MaxRhythms; ++i)
    {
        const juce::String c = "ch" + juce::String(i) + "_";
        const juce::String n = (i < kAutomatedRhythms)
                                   ? "Rhythm " + juce::String(i + 1) + " Ch "
                                   : "Ch"      + juce::String(i + 1) + " ";
        addF(c+"lvl",     n+"Level",      0.0f, 1.0f,  1.0f);  // 0 dB default
        addF(c+"pan",     n+"Pan",       -1.0f, 1.0f,  0.0f);
        addB(c+"mute",    n+"Mute",      false);
        addB(c+"solo",    n+"Solo",      false);
        addF(c+"sendEff", n+"Send Eff",  0.0f, 1.0f,  0.0f);
        addF(c+"sendDly", n+"Send Dly",  0.0f, 1.0f,  0.0f);
        addF(c+"sendRev", n+"Send Rev",  0.0f, 1.0f,  0.0f);
        // Sidechain
        addI(c+"scSrc",   n+"SC Src",    0, 9,     0);  // 0=off, 1-8=ch1-ch8, 9=ext DAW bus
        addF(c+"scAmt",   n+"SC Amount", 0.0f, 1.0f, 0.0f);
        addF(c+"scAtk",   n+"SC Attack", 1.0f, 500.0f, 5.0f);
        addF(c+"scRel",   n+"SC Release",10.0f, 2000.0f, 100.0f);
        // Multi-bus output routing: 0 = Master mix, 1..8 = direct out to Bus 1..8.
        addI(c+"outBus",  n+"Output Bus",0, 8,     0);
    }

    // ── Master loop length — product-specific (sequencer master loop) ─────────
    // mstr_lvl / mstr_pan / mst_ins* + the ret_ return strips are declared by
    // mu_mixfx::addGlobalFxParams above; mstrLoop is mu-clid-only.
    addI("mstrLoop",    "Master Loop",      0,       16,       0);      // 0=free, 1-16 → 16-256 steps

#if MUCLID_LITE_BUILD
    addI("lite_midiNote",   "MIDI Note", 0, 127, 36);
    addF("lite_accentAmt",  "Accent",    0.0f, 100.0f, 0.0f);
#endif

    return layout;
}

//==============================================================================
void PluginProcessor::parameterChanged(const juce::String& id, float v)
{
    // Rhythm params: r{0-7}_{suffix}
    if (id.length() >= 4 && id[0] == 'r' && id[1] >= '0' && id[1] <= '7' && id[2] == '_')
    {
        syncRhythmParam(id[1] - '0', id.substring(3), v);
        return;
    }
    // Shared global-FX / return / master / channel-strip params → mu-core sync
    // (the set declared by mu_mixfx::addGlobalFxParams + the ch{i}_ rhythm strips).
    // mstrLoop is excluded — it has no underscore at [4] so "mstr_" won't match —
    // and is handled below as a product-specific sequencer param.
    if (id.startsWith("eff_") || id.startsWith("rev_") || id.startsWith("dly_") ||
        id.startsWith("eff2") || id.startsWith("dly2") || id.startsWith("echo_") ||
        id.startsWith("ch")   || id.startsWith("ret_") || id.startsWith("mstr_") ||
        id.startsWith("mst_ins"))
    {
        syncGlobalFxParam(id, v);
        return;
    }
    // Master loop length
    if (id == "mstrLoop")
    {
        sequencer.setMasterLoopSteps((int)v * 16);
        return;
    }
}

//==============================================================================
void PluginProcessor::syncRhythmParam(int ri, const juce::String& suffix, float v)
{
    if (ri < 0 || ri >= SequencerEngine::MaxRhythms) return;
    if (ri >= sequencer.getNumRhythms()) return;

    Rhythm& r = sequencer.getRhythm(ri);
    bool patternDirty = false;
    bool voiceDirty   = false;

    // Hold voiceParamsLock for the field mutation so the audio-thread
    // modulation seed copy can't observe a torn write. Spin until acquired
    // (audio side holds it for nanoseconds at a time).
    {
        bool expected = false;
        while (! r.voiceParamsLock.compare_exchange_strong(expected, true, std::memory_order_acquire))
            expected = false;
        applyRhythmSuffix(suffix, v, r, patternDirty, voiceDirty);
        r.voiceParamsLock.store(false, std::memory_order_release);
    }

    if (!apvtsLoading.load(std::memory_order_acquire))
    {
        if (patternDirty) sequencer.updatePattern(ri);
        if (voiceDirty && voiceEngines[ri]) voiceEngines[ri]->setParams(r.voiceParams);
    }
}

// Force-sync a rhythm's full Rhythm struct (HitGenerator state + voiceParams) from
// current APVTS values, regardless of whether parameterChanged would fire. Needed
// after preset reload paths where APVTS values land back on the same numbers they
// held before (e.g. preset A → B → A): JUCE skips listener callbacks on unchanged
// values, so a freshly-constructed Rhythm object at a regrown slot would never get
// its data populated and would play with default `hits=0` patterns (silent) and
// default voice params.
void PluginProcessor::forceSyncRhythmFromAPVTS(int ri)
{
    if (ri < 0 || ri >= SequencerEngine::MaxRhythms) return;
    if (ri >= sequencer.getNumRhythms()) return;

    Rhythm& r = sequencer.getRhythm(ri);
    const juce::String prefix = "r" + juce::String(ri) + "_";

    bool patternDirty = false;
    bool voiceDirty   = false;
    // Acquire/release per-field so the audio thread can squeeze its (very
    // brief) seed-copy lock window in between our writes. Holding the lock
    // across the whole bulk apply (~52 entries) would starve the audio thread
    // for tens of microseconds — unacceptable even though forceSync usually
    // runs from preset / state-restore paths.
    for (int j = 0; j < kRhythmParamCount; ++j)
    {
        if (auto* raw = apvts.getRawParameterValue(prefix + kRhythmParamDefs[j].suffix))
        {
            bool expected = false;
            while (! r.voiceParamsLock.compare_exchange_strong(expected, true, std::memory_order_acquire))
                expected = false;
            applyRhythmSuffix(kRhythmParamDefs[j].suffix, raw->load(), r, patternDirty, voiceDirty);
            r.voiceParamsLock.store(false, std::memory_order_release);
        }
    }

    if (patternDirty) sequencer.updatePattern(ri);
    if (voiceEngines[ri]) voiceEngines[ri]->setParams(r.voiceParams);
}

// syncFXParam / syncMixerParam removed — mu-clid now routes its FX / return /
// master / channel-strip parameterChanged callbacks to the shared
// ProcessorBase::syncGlobalFxParam (mu-core), matching mu-tant. See the backlog.

//==============================================================================
void PluginProcessor::pushRhythmToAPVTS(int ri)
{
    if (ri < 0 || ri >= sequencer.getNumRhythms()) return;
    const Rhythm& r = sequencer.getRhythm(ri);
    const juce::String px = "r" + juce::String(ri) + "_";

    auto set = [this](const juce::String& id, float v)
    {
        if (auto* p = apvts.getParameter(id))
            p->setValueNotifyingHost(p->convertTo0to1(v));
    };

    // iterate the declarative param table instead of hand-listing every
    // (id, value) pair. Each entry's `push` lambda reads the current value from
    // the Rhythm — semantics match the prior code exactly (HitGen ints become
    // floats; ADSR sustain scales 0..1 → 0..100; aEnvRel reflects the
    // ampRelToEnd sentinel; bool fields write 0.0 / 1.0).
    for (int i = 0; i < mu_pp::kRhythmParamCount; ++i)
    {
        const auto& def = mu_pp::kRhythmParamDefs[i];
        set(px + def.suffix, def.push(r));
    }
}

void PluginProcessor::pushMixerChannelToAPVTS(int idx)
{
    if (idx < 0 || idx >= SequencerEngine::MaxRhythms) return;
    const auto& ch = mixerEngine.channels[idx];
    const juce::String px = "ch" + juce::String(idx) + "_";

    auto set = [this](const juce::String& id, float v)
    {
        if (auto* p = apvts.getParameter(id))
            p->setValueNotifyingHost(p->convertTo0to1(v));
    };

    set(px+"lvl",     ch.level);
    set(px+"pan",     ch.pan);
    set(px+"mute",    ch.mute ? 1.0f : 0.0f);
    set(px+"solo",    ch.solo ? 1.0f : 0.0f);
    set(px+"sendEff", ch.sendEffect);
    set(px+"sendDly", ch.sendDelay);
    set(px+"sendRev", ch.sendReverb);
    // Sidechain + bus routing: previously missing here, so after a sidebar reorder
    // the in-memory swap was correct but APVTS still held the old slot's values,
    // and the APVTS->engine listener would overwrite the engine back to stale state.
    set(px+"scSrc",   (float)(ch.sidechainSource + 1));  // engine -1..7 → APVTS 0..8
    set(px+"scAmt",   ch.sidechainAmount);
    set(px+"scAtk",   ch.sidechainAttackMs);
    set(px+"scRel",   ch.sidechainReleaseMs);
    set(px+"outBus",  (float)ch.outputBus);
}

void PluginProcessor::swapAPVTSForRhythms(int i, int j)
{
    mu_core::ScopedApvtsLoading guard(apvtsLoading);
    pushRhythmToAPVTS(i);
    pushRhythmToAPVTS(j);
    // Push every active channel: not just i and j, because sidechain source
    // indices on OTHER channels may have been re-translated by the swap
    // (any channel pointing at i now points at j, and vice versa).
    const int n = numActiveRhythms.load(std::memory_order_acquire);
    for (int c = 0; c < n; ++c)
        pushMixerChannelToAPVTS(c);
}
