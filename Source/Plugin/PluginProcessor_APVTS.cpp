// partial-class TU split from PluginProcessor.cpp. Contains:
// - createParameterLayout (the big APVTS layout factory)
// - parameterChanged dispatcher
// - syncRhythmParam / forceSyncRhythmFromAPVTS / syncFXParam / syncMixerParam
// - pushRhythmToAPVTS / pushMixerChannelToAPVTS / swapAPVTSForRhythms
//
// Shared helpers (kRhythmSuffixes, kChannelSuffixes, kGlobalParams, applyRhythmSuffix,
// adsrTime, adsrSus) live in PluginProcessor_Internal.h since the Preset TU needs them too.

#include "PluginProcessor.h"
#include "PluginProcessor_Internal.h"
#include "Audio/FX/Slots/FXAlgorithmDef.h"

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
        addI(p+"logic", n+"Logic", 0, 4, 0);
        addB(p+"patLeg", n+"Pattern Legato", false);
        // Pitch
        addI(p+"pitchOct",  n+"Pitch Oct",  -4,   4,  0);
        addI(p+"pitchSemi", n+"Pitch Semi", -12,  12,  0);
        addF(p+"pitchFine", n+"Pitch Fine", -100.0f, 100.0f, 0.0f);
        addAdsrT(p+"pEnvAtk", n+"P Env Atk", 0.0f);    // seconds (≈ legacy 0.0 × 0.03)
        addAdsrT(p+"pEnvDec", n+"P Env Dec", 0.03f);   //         (≈ legacy 1.0 × 0.03)
        addF     (p+"pEnvSus", n+"P Env Sus", 0.0f, 100.0f, 0.0f);   // sustain stays 0..100 %
        addAdsrT(p+"pEnvRel", n+"P Env Rel", 0.03f);   //         (≈ legacy 1.0 × 0.03)
        addF(p+"pEnvDep",   n+"P Env Dep",  0.0f,  24.0f,  0.0f);
        addB(p+"pEnvLeg",   n+"P Env Legato", false);
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
        addB(p+"fEnvLeg", n+"F Env Legato", false);
        // Amp
        addF(p+"ampLvl",  n+"Amp Level",  0.0f,   2.0f,  1.0f);  // Issue #121: 0 dB default
        addAdsrT(p+"aEnvAtk", n+"A Env Atk", 0.005f);   // seconds
        addAdsrT(p+"aEnvDec", n+"A Env Dec", 0.3f);
        addF(p+"aEnvSus", n+"A Env Sus",  0.0f, 100.0f, 80.0f);   // sustain stays 0..100 %
        addAdsrT(p+"aEnvRel", n+"A Env Rel", 0.5f);
        addB(p+"aEnvLeg",   n+"A Env Legato", false);
        addF(p+"accentDb",  n+"Accent",     0.0f,  12.0f,  0.0f);
        // Drive
        addI(p+"drvChar",    n+"Drive Char",   0,     12,      0);  // 0=None … 10=TapeSat, 11=Karplus (#422), 12=Vocoder (#423)
        addF(p+"drvDrv",     n+"Drive",        0.0f, 100.0f,    0.0f);  // Soft/Hard/Fold drive amount
        addF(p+"drvOut",     n+"Drive Out",  -24.0f,   24.0f,   0.0f);  // insert output / makeup gain
        addF(p+"drvBits",    n+"Bits",         0.0f,  16.0f,   16.0f);  // Bitcrusher bit depth (#429: range extended to 0 so Karplus Octave 0 fits)
        addF(p+"drvRate",    n+"Drive Rate",  100.0f, 48000.0f, 48000.0f);  // Bitcrusher sample rate
        addF(p+"drvDit",     n+"Dither",       0.0f, 100.0f,    0.0f);  // Bitcrusher dither amount
        addF(p+"drvTon",     n+"Drive Tone",  20.0f, 20000.0f, 20000.0f);  // Shared LPF
        addF(p+"eqMidGain",  n+"EQ Mid Gain",-18.0f,  18.0f,   0.0f);  // EQ mid-band gain (#129)
    }

    // ── Effect slot (8 params) ────────────────────────────────────────────────
    addI("eff_algo", "Effect Algorithm", 0, 7, 0);
    addB("eff_en",   "Effect Enable", true);
    addF("eff_send", "Effect Send",  0.0f, 1.0f, 1.0f);
    // Generic normalized (0–1) slots for algorithm-specific params.
    // Actual value = paramDef.minVal + stored * (paramDef.maxVal - paramDef.minVal).
    addF("eff_p0", "Effect P0", 0.0f, 1.0f, 0.5f);
    addF("eff_p1", "Effect P1", 0.0f, 1.0f, 0.5f);
    addF("eff_p2", "Effect P2", 0.0f, 1.0f, 0.5f);
    addF("eff_p3", "Effect P3", 0.0f, 1.0f, 0.5f);
    addF("eff_p4", "Effect P4", 0.0f, 1.0f, 0.5f);

    // ── Delay slot (11 params) ────────────────────────────────────────────────
    addB("dly_en",        "Delay Enable",    true);
    addB("dly_mode",      "Delay Sync",      false);
    addF("dly_ms",        "Delay Time (ms)", 1.0f, 4000.0f, 250.0f);
    addI("dly_syncDenom", "Delay Denom",     0, 3, 3);   // 0=1/32, 1=1/16, 2=1/8, 3=1/4
    addB("dly_syncDot",   "Delay Dotted",    false);
    addB("dly_syncTrip",  "Delay Triplet",   false);
    addI("dly_count",     "Delay Count",     1, 8, 1);
    addF("dly_fb",        "Delay Feedback",  0.0f,  0.98f, 0.45f);
    addF("dly_spread",    "Delay Spread",    0.0f,  1.0f,  0.0f);
    addF("dly_dirt",      "Delay Dirt",      0.0f,  1.0f,  0.0f);
    addF("dly_send",      "Delay Send",      0.0f,  1.0f,  1.0f);

    // ── Reverb slot (9 params) ────────────────────────────────────────────────
    addI("rev_algo", "Reverb Algorithm",  0, 3,  0);
    addB("rev_en",   "Reverb Enable",     true);
    addF("rev_lvl",  "Reverb Level",  0.0f,   1.0f,  1.0f);
    addF("rev_size", "Reverb Size",   0.0f,   1.0f,  0.5f);
    addF("rev_pre",  "Reverb Pre-Delay", 0.0f, 100.0f, 10.0f);
    addF("rev_diff", "Reverb Diffusion", 0.0f,   1.0f,  0.7f);
    addF("rev_damp", "Reverb Damp",   0.0f,   1.0f,  0.4f);
    addF("rev_mod",  "Reverb Mod",    0.0f,   1.0f,  0.2f);
    addF("rev_dirt", "Reverb Dirt",   0.0f,   1.0f,  0.0f);

    // ── Intra-FX routing (3 params) ───────────────────────────────────────────
    addF("eff2dly", juce::String::fromUTF8(u8"Effect→Delay"),  0.0f, 1.0f, 0.0f);
    addF("eff2rev", juce::String::fromUTF8(u8"Effect→Reverb"), 0.0f, 1.0f, 0.0f);
    addF("dly2rev", juce::String::fromUTF8(u8"Delay→Reverb"),  0.0f, 1.0f, 0.0f);

    // ── Echo (embedded in EFX slot when algo=Echo) ────────────────────────────
    addB("echo_en",        "Echo Enable",      true);
    addF("echo_mode",      "Echo Mode",        0.0f, 1.0f, 0.0f);
    addF("echo_ms",        "Echo Time Ms",     1.0f, 4000.0f, 250.0f);
    addI("echo_syncDenom", "Echo Sync Denom",  0, 3, 2);
    addB("echo_syncDot",   "Echo Dotted",      false);
    addB("echo_syncTrip",  "Echo Triplet",     false);
    addI("echo_count",     "Echo Count",       1, 8, 1);
    addF("echo_fb",        "Echo Feedback",    0.0f, 1.0f, 0.45f);
    addF("echo_spread",    "Echo Spread",      0.0f, 1.0f, 0.0f);
    addF("echo_dirt",      "Echo Dirt",        0.0f, 1.0f, 0.0f);

    // ── Rhythm channel strips (11 × 8 = 88) ──────────────────────────────────
    for (int i = 0; i < SequencerEngine::MaxRhythms; ++i)
    {
        const juce::String c = "ch" + juce::String(i) + "_";
        const juce::String n = (i < kAutomatedRhythms)
                                   ? "Rhythm " + juce::String(i + 1) + " Ch "
                                   : "Ch"      + juce::String(i + 1) + " ";
        addF(c+"lvl",     n+"Level",      0.0f, 1.0f,  1.0f);  // Issue #121: 0 dB default
        addF(c+"pan",     n+"Pan",       -1.0f, 1.0f,  0.0f);
        addB(c+"mute",    n+"Mute",      false);
        addB(c+"solo",    n+"Solo",      false);
        addF(c+"sendEff", n+"Send Eff",  0.0f, 1.0f,  0.0f);
        addF(c+"sendDly", n+"Send Dly",  0.0f, 1.0f,  0.0f);
        addF(c+"sendRev", n+"Send Rev",  0.0f, 1.0f,  0.0f);
        // Sidechain
        addI(c+"scSrc",   n+"SC Src",    0, 8,     0);  // 0=off, 1-8=ch1-ch8
        addF(c+"scAmt",   n+"SC Amount", 0.0f, 1.0f, 0.0f);
        addF(c+"scAtk",   n+"SC Attack", 1.0f, 500.0f, 5.0f);
        addF(c+"scRel",   n+"SC Release",10.0f, 2000.0f, 100.0f);
        // Multi-bus output routing: 0 = Master mix, 1..8 = direct out to Bus 1..8.
        addI(c+"outBus",  n+"Output Bus",0, 8,     0);
    }

    // ── Return channel strips (8 × 3 = 24) ───────────────────────────────────
    for (const char* ret : { "eff", "dly", "rev" })
    {
        const juce::String q = juce::String("ret_") + ret + "_";
        const juce::String nm = juce::String("Ret ") + ret + " ";
        addF(q+"lvl",   nm+"Level",    0.0f,    1.0f,    0.75f);
        addF(q+"pan",   nm+"Pan",     -1.0f,    1.0f,    0.0f);
        addB(q+"mute",  nm+"Mute",    false);
        addB(q+"solo",  nm+"Solo",    false);
        addI(q+"scSrc", nm+"SC Src",  0,        8,       0);      // 0=off, 1-8=ch0-ch7
        addF(q+"scAmt", nm+"SC Amt",  0.0f,     1.0f,    0.0f);
        addF(q+"scAtk", nm+"SC Atk",  1.0f,   500.0f,    5.0f);
        addF(q+"scRel", nm+"SC Rel",  10.0f, 2000.0f,  100.0f);
    }

    // ── Master (2 params + 8 insert params) ──────────────────────────────────
    addF("mstr_lvl",    "Master Level",     0.0f,    1.0f,     1.0f);   // Issue #121: 0 dB default
    addF("mstr_pan",    "Master Pan",      -1.0f,    1.0f,     0.0f);
    addI("mstrLoop",    "Master Loop",      0,       16,       0);      // 0=free, 1-16 → 16-256 steps
    // Master insert effect (#124): same algorithm set as per-rhythm voice INSERT.
    addI("mst_insChar", "Mst Insert Char",  0,       12,       0);      // 0=None … 10=TapeSat, 11=Karplus, 12=Vocoder
    addF("mst_insDrv",  "Mst Insert Drive", 0.0f,  100.0f,     0.0f);
    addF("mst_insOut",  "Mst Insert Out", -24.0f,   24.0f,     0.0f);
    addF("mst_insBits", "Mst Insert Bits",  0.0f,   16.0f,    16.0f);
    addF("mst_insRate", "Mst Insert Rate", 100.0f, 48000.0f, 48000.0f);
    addF("mst_insDit",  "Mst Insert Dit",   0.0f,  100.0f,     0.0f);
    addF("mst_insTon",  "Mst Insert Tone", 20.0f, 20000.0f, 20000.0f);
    addF("mst_insMid",  "Mst Insert Mid", -18.0f,   18.0f,     0.0f);  // EQ mid gain
    // Master insert 2 (#283): chained after insert 1.
    addI("mst_ins2Char","Mst Insert2 Char", 0,       12,       0);
    addF("mst_ins2Drv", "Mst Insert2 Drive",0.0f,  100.0f,     0.0f);
    addF("mst_ins2Out", "Mst Insert2 Out",-24.0f,   24.0f,     0.0f);
    addF("mst_ins2Bits","Mst Insert2 Bits", 0.0f,   16.0f,    16.0f);
    addF("mst_ins2Rate","Mst Insert2 Rate",100.0f,48000.0f, 48000.0f);
    addF("mst_ins2Dit", "Mst Insert2 Dit",  0.0f,  100.0f,     0.0f);
    addF("mst_ins2Ton", "Mst Insert2 Tone",20.0f, 20000.0f, 20000.0f);
    addF("mst_ins2Mid", "Mst Insert2 Mid",-18.0f,   18.0f,     0.0f);

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
    // FX params
    if (id.startsWith("eff_") || id.startsWith("rev_") || id.startsWith("dly_") ||
        id.startsWith("eff2") || id.startsWith("dly2") || id.startsWith("echo_"))
    {
        syncFXParam(id, v);
        return;
    }
    // Mixer params
    if (id.startsWith("ch") || id.startsWith("ret_") || id.startsWith("mstr_") || id.startsWith("mst_ins"))
    {
        syncMixerParam(id, v);
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
    applyRhythmSuffix(suffix, v, r, patternDirty, voiceDirty);

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
    for (int j = 0; j < kRhythmParamCount; ++j)
    {
        if (auto* raw = apvts.getRawParameterValue(prefix + kRhythmParamDefs[j].suffix))
            applyRhythmSuffix(kRhythmParamDefs[j].suffix, raw->load(), r, patternDirty, voiceDirty);
    }

    if (patternDirty) sequencer.updatePattern(ri);
    if (voiceEngines[ri]) voiceEngines[ri]->setParams(r.voiceParams);
}

void PluginProcessor::syncFXParam(const juce::String& id, float v)
{
    auto& eff = fxChain.effectSlot();
    auto& dly = fxChain.delaySlot();
    auto& rev = fxChain.reverbSlot();

    // do NOT guard on apvtsLoading — that left the engine at the default
    // algorithm (0=Chorus) when state restore loaded eff_algo=N, because nothing
    // re-synced the engine after the load completed. Then the user trying to
    // change the algorithm via the dropdown triggered the #285 listener-skip-
    // on-unchanged-value bug (APVTS already at N, no notification, engine stuck
    // at 0), and the next loadFromAPVTS read engine state and snapped the FXRow
    // back to Chorus. setAlgorithm is cheap (one make_unique + prepare).
    if      (id == "eff_algo") { eff.setAlgorithm((int)v); }
    else if (id == "eff_en")   { eff.setEnabled(v > 0.5f); }
    else if (id.startsWith("eff_p"))
    {
        int idx = id.substring(5).getIntValue();
        const auto& algos = FXAlgorithmRegistry::effectAlgorithms();
        int ai = eff.getAlgorithmIndex();
        if (ai < (int)algos.size() && idx < (int)algos[ai].params.size())
        {
            const auto& pd = algos[ai].params[idx];
            eff.setParam(pd.id, pd.minVal + v * (pd.maxVal - pd.minVal));
        }
    }
    else if (id == "dly_en")   { dly.setEnabled(v > 0.5f); }
    else if (id == "dly_mode") { dly.setTimeMode(v > 0.5f ? DelaySlot::TimeMode::Sync : DelaySlot::TimeMode::Free); }
    else if (id == "dly_ms")   { dly.setDelayMs(v); }
    else if (id == "dly_syncDenom" || id == "dly_syncDot" || id == "dly_syncTrip")
    {
        static const int denoms[] = { 32, 16, 8, 4 };
        int idx  = juce::jlimit(0, 3, (int)*apvts.getRawParameterValue("dly_syncDenom"));
        bool dot = *apvts.getRawParameterValue("dly_syncDot")  > 0.5f;
        bool tri = *apvts.getRawParameterValue("dly_syncTrip") > 0.5f;
        dly.setTimeDivision(denoms[idx], dot, tri);
    }
    else if (id == "dly_count")  { dly.setTimeCount(juce::jmax(1, (int)v)); }
    else if (id == "dly_fb")     { dly.setFeedback(v); }
    else if (id == "dly_spread") { dly.setSpread(v); }
    else if (id == "dly_dirt")   { dly.setDirt(v); }
    else if (id == "dly_send")   { dly.setSend(v); }
    else if (id == "rev_algo")   { rev.setAlgorithm((int)v); }   // see eff_algo
    else if (id == "rev_en")     { rev.setEnabled(v > 0.5f); }
    else if (id == "rev_lvl")    { rev.setLevel(v); }
    else if (id == "rev_size")   { rev.setParam("size",      v); }
    else if (id == "rev_pre")    { rev.setParam("predelay",  v); }
    else if (id == "rev_diff")   { rev.setParam("diffusion", v); }
    else if (id == "rev_damp")   { rev.setParam("damp",      v); }
    else if (id == "rev_mod")    { rev.setParam("mod",       v); }
    else if (id == "rev_dirt")   { rev.setParam("dirt",      v); }
    else if (id == "eff2dly")    { fxChain.setEffectToDelaySend(v); }
    else if (id == "eff2rev")    { fxChain.setEffectToReverbSend(v); }
    else if (id == "dly2rev")    { fxChain.setDelayToReverbSend(v); }
    else if (id == "echo_en")    { eff.getEchoDelay().setEnabled(v > 0.5f); }
    else if (id == "echo_mode")  { eff.getEchoDelay().setTimeMode(v > 0.5f ? DelaySlot::TimeMode::Sync : DelaySlot::TimeMode::Free); }
    else if (id == "echo_ms")    { eff.getEchoDelay().setDelayMs(v); }
    else if (id == "echo_syncDenom" || id == "echo_syncDot" || id == "echo_syncTrip")
    {
        static const int denoms[] = { 32, 16, 8, 4 };
        int  idx = juce::jlimit(0, 3, (int)*apvts.getRawParameterValue("echo_syncDenom"));
        bool dot = *apvts.getRawParameterValue("echo_syncDot")  > 0.5f;
        bool tri = *apvts.getRawParameterValue("echo_syncTrip") > 0.5f;
        eff.getEchoDelay().setTimeDivision(denoms[idx], dot, tri);
    }
    else if (id == "echo_count")  { eff.getEchoDelay().setTimeCount(juce::jmax(1, (int)v)); }
    else if (id == "echo_fb")     { eff.getEchoDelay().setFeedback(v); }
    else if (id == "echo_spread") { eff.getEchoDelay().setSpread(v); }
    else if (id == "echo_dirt")   { eff.getEchoDelay().setDirt(v); }
}

void PluginProcessor::syncMixerParam(const juce::String& id, float v)
{
    // ch{0-7}_{param}
    if (id.length() >= 6 && id[0] == 'c' && id[1] == 'h' && id[3] == '_')
    {
        int i = id[2] - '0';
        if (i >= 0 && i < SequencerEngine::MaxRhythms)
        {
            auto& ch = mixerEngine.channels[i];
            const juce::String param = id.substring(4);
            if      (param == "lvl")     ch.level      = v;
            else if (param == "pan")     ch.pan        = v;
            else if (param == "mute")    ch.mute       = v > 0.5f;
            else if (param == "solo")    ch.solo       = v > 0.5f;
            else if (param == "sendEff") ch.sendEffect = v;
            else if (param == "sendDly") ch.sendDelay  = v;
            else if (param == "sendRev") ch.sendReverb = v;
            else if (param == "scSrc")   ch.sidechainSource   = juce::roundToInt(v) - 1;
            else if (param == "scAmt")   ch.sidechainAmount   = v;
            else if (param == "scAtk")   ch.sidechainAttackMs  = v;
            else if (param == "scRel")   ch.sidechainReleaseMs = v;
            else if (param == "outBus")  ch.outputBus         = juce::jlimit(0, 8, juce::roundToInt(v));
        }
        return;
    }

    // ret_{eff|dly|rev}_{param}
    if (id.startsWith("ret_"))
    {
        int retIdx = -1;
        juce::String rest;
        if      (id.startsWith("ret_eff_")) { retIdx = 0; rest = id.substring(8); }
        else if (id.startsWith("ret_dly_")) { retIdx = 1; rest = id.substring(8); }
        else if (id.startsWith("ret_rev_")) { retIdx = 2; rest = id.substring(8); }

        if (retIdx >= 0)
        {
            auto& ret = mixerEngine.returns[retIdx];
            if      (rest == "lvl")   ret.level             = v;
            else if (rest == "pan")   ret.pan               = v;
            else if (rest == "mute")  ret.mute              = v > 0.5f;
            else if (rest == "solo")  ret.solo              = v > 0.5f;
            else if (rest == "scSrc") ret.sidechainSource   = juce::jlimit(0, 8, (int)v) - 1; // 0→-1 (off), 1-8→0-7
            else if (rest == "scAmt") ret.sidechainAmount   = juce::jlimit(0.0f, 1.0f, v);
            else if (rest == "scAtk") ret.sidechainAttackMs  = v;
            else if (rest == "scRel") ret.sidechainReleaseMs = v;
        }
        return;
    }

    if      (id == "mstr_lvl") mixerEngine.masterLevel = v;
    else if (id == "mstr_pan") mixerEngine.masterPan   = v;
    else if (id == "mst_insChar")  mixerEngine.masterInsertParams.insertAlgo  = juce::jlimit(0, 12, (int)v);
    else if (id == "mst_insDrv")   mixerEngine.masterInsertParams.insertDrive = v;
    else if (id == "mst_insOut")   mixerEngine.masterInsertParams.insertOutput= v;
    else if (id == "mst_insBits")  mixerEngine.masterInsertParams.insertBits    = v;
    else if (id == "mst_insRate")  mixerEngine.masterInsertParams.insertRate  = v;
    else if (id == "mst_insDit")   mixerEngine.masterInsertParams.insertDither  = v;
    else if (id == "mst_insTon")   mixerEngine.masterInsertParams.insertTone  = v;
    else if (id == "mst_insMid")   mixerEngine.masterInsertParams.insertEqMid  = v;
    else if (id == "mst_ins2Char") mixerEngine.masterInsertParams2.insertAlgo  = juce::jlimit(0, 12, (int)v);
    else if (id == "mst_ins2Drv")  mixerEngine.masterInsertParams2.insertDrive = v;
    else if (id == "mst_ins2Out")  mixerEngine.masterInsertParams2.insertOutput= v;
    else if (id == "mst_ins2Bits") mixerEngine.masterInsertParams2.insertBits    = v;
    else if (id == "mst_ins2Rate") mixerEngine.masterInsertParams2.insertRate  = v;
    else if (id == "mst_ins2Dit")  mixerEngine.masterInsertParams2.insertDither  = v;
    else if (id == "mst_ins2Ton")  mixerEngine.masterInsertParams2.insertTone  = v;
    else if (id == "mst_ins2Mid")  mixerEngine.masterInsertParams2.insertEqMid  = v;
}

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
