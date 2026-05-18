#pragma once

// #365: shared helpers used by the partial-class TUs that PluginProcessor.cpp was
// split into (PluginProcessor.cpp + PluginProcessor_APVTS.cpp + PluginProcessor_Preset.cpp).
// All members are inline so each TU sees the same definition without an ODR violation.
// Private to the PluginProcessor implementation — do not include from other code.

#include <juce_core/juce_core.h>
#include "Sequencer/Rhythm.h"
#include "ScopedApvtsLoading.h"   // #409: lifted to mu-core — reuse from there.

namespace mu_pp {

// ── ADSR display-scale converters ────────────────────────────────────────────
// Convert 0–100 UI scale → 0..3 s for ADSR time params.
inline float adsrTime(float v) noexcept { return juce::jmax(0.001f, v * 0.03f); }
// Convert 0–100 UI scale → 0–1 amplitude for ADSR Sustain.
inline float adsrSus (float v) noexcept { return juce::jlimit(0.0f, 1.0f, v / 100.0f); }

// ── All per-rhythm APVTS parameter suffixes (used for rhythm preset save/load). ─
inline const char* const kRhythmSuffixes[] = {
    "stepsA","hitsA","rotA","prePadA","postPadA","insStA","insLenA","insModeA","prePadModeA","postPadModeA",
    "stepsB","hitsB","rotB","prePadB","postPadB","insStB","insLenB","insModeB","prePadModeB","postPadModeB",
    "stepsC","hitsC","rotC","prePadC","postPadC","insStC","insLenC","insModeC","prePadModeC","postPadModeC",
    "logic","patLeg",
    "pitchOct","pitchSemi","pitchFine","pEnvAtk","pEnvDec","pEnvSus","pEnvRel","pEnvDep","pEnvLeg",
    "fltType","fltCut","fltRes","fEnvAtk","fEnvDec","fEnvSus","fEnvRel","fEnvDep","fEnvLeg",
    "ampLvl","aEnvAtk","aEnvDec","aEnvSus","aEnvRel","aEnvLeg","accentDb",
    "drvChar","drvDrv","drvOut","drvBits","drvRate","drvDit","drvTon","eqMidGain",
    nullptr
};

// Per-channel mixer APVTS parameter suffixes (prefix: "ch{i}_").
// Saved in rhythm presets so sends/sidechain travel with the rhythm.
inline const char* const kChannelSuffixes[] = {
    "lvl","pan","mute","solo","sendEff","sendDly","sendRev",
    "scSrc","scAmt","scAtk","scRel","outBus",
    nullptr
};

// Global APVTS parameter IDs written to the GlobalState child of .muclid presets.
inline const char* const kGlobalParams[] = {
    "eff_algo","eff_en","eff_send","eff_p0","eff_p1","eff_p2","eff_p3","eff_p4",
    "dly_en","dly_mode","dly_ms","dly_syncDenom","dly_syncDot","dly_syncTrip",
    "dly_count","dly_fb","dly_spread","dly_dirt","dly_send",
    "rev_algo","rev_en","rev_lvl","rev_size","rev_pre","rev_diff","rev_damp","rev_mod","rev_dirt",
    "eff2dly","eff2rev","dly2rev",
    "echo_en","echo_mode","echo_ms","echo_syncDenom","echo_syncDot","echo_syncTrip",
    "echo_count","echo_fb","echo_spread","echo_dirt",
    "ret_eff_lvl","ret_eff_pan","ret_eff_mute","ret_eff_solo",
    "ret_eff_scSrc","ret_eff_scAmt","ret_eff_scAtk","ret_eff_scRel",
    "ret_dly_lvl","ret_dly_pan","ret_dly_mute","ret_dly_solo",
    "ret_dly_scSrc","ret_dly_scAmt","ret_dly_scAtk","ret_dly_scRel",
    "ret_rev_lvl","ret_rev_pan","ret_rev_mute","ret_rev_solo",
    "ret_rev_scSrc","ret_rev_scAmt","ret_rev_scAtk","ret_rev_scRel",
    "mstr_lvl","mstr_pan","mstrLoop",
    "mst_insChar","mst_insDrv","mst_insOut","mst_insBits","mst_insRate","mst_insDit","mst_insTon","mst_insMid",
    "mst_ins2Char","mst_ins2Drv","mst_ins2Out","mst_ins2Bits","mst_ins2Rate","mst_ins2Dit","mst_ins2Ton","mst_ins2Mid",
    nullptr
};

// Applies a rhythm parameter suffix + display-scale value to a Rhythm struct.
// Returns patternDirty (true) or voiceDirty (false) via the out-params.
// Called from syncRhythmParam (APVTS TU) and stageRhythmPreset (Preset TU).
// #430: legacy-preset migration for normalized parameter values.
//
// .muRhyth + .muclid preset files store param->getValue() (normalized 0..1).
// On load, the same normalized value is fed back via setValueNotifyingHost(),
// which JUCE re-interprets against the parameter's CURRENT range. When a range
// is widened later (e.g. drvChar 0..10 → 0..12 in #422/#423; drvBits 1..16 →
// 0..16 in #429), an old normalized value resolves to a different actual value.
//
// Symptom: a pre-#422 preset using TapeSat (drvChar=10, norm=1.0) loads as
// Vocoder (drvChar=12 under the new range) and its drvDrv value (saved 0..100)
// gets reinterpreted as Vocoder waveshape (0..3 clamped) → noise carrier.
//
// Fix: any preset lacking a `presetVersion` property is treated as legacy and
// gets its normalized values rescaled here so the int/float resolves to the
// SAME actual value under the new range.
//
// Returns the migrated normalized value. Pass through unchanged for suffixes
// that aren't affected by a range change.
inline float migrateLegacyPresetNorm(const juce::String& suffix, float oldNorm) noexcept
{
    // Per-rhythm: drvChar (was int 0..10, now 0..12).
    //   v_old = round(oldNorm * 10), v_new = round(newNorm * 12)
    //   To preserve v: newNorm = v / 12 = (oldNorm * 10) / 12
    if (suffix == "drvChar")
        return juce::jlimit(0.0f, 1.0f, oldNorm * (10.0f / 12.0f));

    // Per-rhythm: drvBits (was float 1..16, now 0..16).
    //   v_old = 1 + oldNorm * 15, v_new = newNorm * 16
    //   To preserve v: newNorm = (1 + oldNorm * 15) / 16
    if (suffix == "drvBits")
        return juce::jlimit(0.0f, 1.0f, (1.0f + oldNorm * 15.0f) / 16.0f);

    return oldNorm;
}

// Same migration for the .muclid GlobalState child's master-insert params
// (kGlobalParams entries). Keyed on the full param ID, not a suffix.
inline float migrateLegacyGlobalNorm(const juce::String& id, float oldNorm) noexcept
{
    if (id == "mst_insChar" || id == "mst_ins2Char")
        return juce::jlimit(0.0f, 1.0f, oldNorm * (10.0f / 12.0f));
    if (id == "mst_insBits" || id == "mst_ins2Bits")
        return juce::jlimit(0.0f, 1.0f, (1.0f + oldNorm * 15.0f) / 16.0f);
    return oldNorm;
}

// Current preset-file schema version. Bumped whenever a parameter range change
// would invalidate older preset files. Files without this property are treated
// as v0 and run through migrateLegacyPresetNorm / migrateLegacyGlobalNorm.
inline constexpr int kCurrentPresetVersion = 1;

inline void applyRhythmSuffix(const juce::String& suffix, float v, Rhythm& r,
                               bool& patternDirty, bool& voiceDirty)
{
    auto applyHitGen = [&](HitGenerator& gen, const juce::String& s, float val)
    {
        if      (s == "steps")       { gen.steps        = juce::jlimit(1, 64, (int)val); patternDirty = true; }
        else if (s == "hits")        { gen.hits          = juce::jlimit(0, 64, (int)val); patternDirty = true; }
        else if (s == "rot")         { gen.rotate        = (int)val;                      patternDirty = true; }
        else if (s == "prePad")      { gen.prePad        = juce::jlimit(0, 12, (int)val); patternDirty = true; }
        else if (s == "postPad")     { gen.postPad       = juce::jlimit(0, 12, (int)val); patternDirty = true; }
        else if (s == "insSt")       { gen.insertStart   = juce::jlimit(0, 63, (int)val); patternDirty = true; }
        else if (s == "insLen")      { gen.insertLength  = juce::jlimit(0,  8, (int)val); patternDirty = true; }
        else if (s == "insMode")     { gen.insertMode   = val > 0.5f ? InsertMode::Mute : InsertMode::Pad; patternDirty = true; }
        else if (s == "prePadMode")  { gen.prePadMode   = val > 0.5f ? InsertMode::Mute : InsertMode::Pad; patternDirty = true; }
        else if (s == "postPadMode") { gen.postPadMode  = val > 0.5f ? InsertMode::Mute : InsertMode::Pad; patternDirty = true; }
    };

    if      (suffix.endsWith("A")) applyHitGen(r.genA, suffix.dropLastCharacters(1), v);
    else if (suffix.endsWith("B")) applyHitGen(r.genB, suffix.dropLastCharacters(1), v);
    else if (suffix.endsWith("C")) applyHitGen(r.genC, suffix.dropLastCharacters(1), v);
    else if (suffix == "logic")     { r.logic = static_cast<Logic>(juce::jlimit(0, 4, (int)v)); patternDirty = true; }
    else if (suffix == "patLeg")    { r.patternLegato = (v > 0.5f); /* sequencer-level, no engine sync needed */ }   // #419
    else if (suffix == "pitchOct")  { r.voiceParams.pitchOctave    = juce::jlimit(-4, 4, (int)v);   voiceDirty = true; }
    else if (suffix == "pitchSemi") { r.voiceParams.pitchSemitones = juce::jlimit(-12, 12, (int)v); voiceDirty = true; }
    else if (suffix == "pitchFine") { r.voiceParams.pitchFine      = v;  voiceDirty = true; }
    // #287 — pEnvAtk/Dec/Rel stored in seconds directly (0..10 s, skew 0.3).
    // #420: floor moved to VoiceEngine::syncEnvelopes (JUCE-API boundary). Storing
    // raw v here keeps voiceParams faithful to the user's actual knob value, so
    // a refreshSuffix() reload after touching another knob doesn't re-display
    // a fractionally-floored value (the prior `jmax(0.001f, v)` made an APVTS
    // value of 0 round-trip through voiceParams as 0.001 → visible knob creep).
    else if (suffix == "pEnvAtk")   { r.voiceParams.pitchEnvAtk    = v;            voiceDirty = true; }
    else if (suffix == "pEnvDec")   { r.voiceParams.pitchEnvDec    = v;            voiceDirty = true; }
    else if (suffix == "pEnvSus")   { r.voiceParams.pitchEnvSus    = adsrSus(v);   voiceDirty = true; }
    else if (suffix == "pEnvRel")   { r.voiceParams.pitchEnvRel    = v;            voiceDirty = true; }
    else if (suffix == "pEnvDep")   { r.voiceParams.pitchEnvDepth  = v;            voiceDirty = true; }
    else if (suffix == "pEnvLeg")   { r.voiceParams.pitchEnvLegato = (v > 0.5f);   voiceDirty = true; }   // #221
    else if (suffix == "fltType")   { r.voiceParams.filterType     = juce::jlimit(0, 15, (int)v); voiceDirty = true; }
    else if (suffix == "fltCut")    { r.voiceParams.filterCutoff   = v;            voiceDirty = true; }
    else if (suffix == "fltRes")    { r.voiceParams.filterRes      = v;            voiceDirty = true; }
    // #286 — fEnvAtk/Dec/Rel stored in seconds directly (0..10 s, skew 0.3).
    else if (suffix == "fEnvAtk")   { r.voiceParams.filterEnvAtk   = v;            voiceDirty = true; }   // #420
    else if (suffix == "fEnvDec")   { r.voiceParams.filterEnvDec   = v;            voiceDirty = true; }   // #420
    else if (suffix == "fEnvSus")   { r.voiceParams.filterEnvSus   = adsrSus(v);   voiceDirty = true; }
    else if (suffix == "fEnvRel")   { r.voiceParams.filterEnvRel   = v;            voiceDirty = true; }   // #420
    else if (suffix == "fEnvDep")   { r.voiceParams.filterEnvDepth = v;            voiceDirty = true; }
    else if (suffix == "fEnvLeg")   { r.voiceParams.filterEnvLegato = (v > 0.5f);  voiceDirty = true; }   // #221
    else if (suffix == "ampLvl")    { r.voiceParams.ampLevel       = v;            voiceDirty = true; }
    else if (suffix == "aEnvAtk")   { r.voiceParams.ampEnvAtk      = v;            voiceDirty = true; }   // #420 (#217 seconds)
    else if (suffix == "aEnvDec")   { r.voiceParams.ampEnvDec      = v;            voiceDirty = true; }   // #420 (#217 seconds)
    else if (suffix == "aEnvSus")   { r.voiceParams.ampEnvSus      = adsrSus(v);  voiceDirty = true; }
    else if (suffix == "aEnvRel")   { r.voiceParams.ampEnvRel = v; r.voiceParams.ampRelToEnd = (v >= 10.0f); voiceDirty = true; }   // #420 + #217 seconds + End at new max
    else if (suffix == "aEnvLeg")   { r.voiceParams.ampEnvLegato = (v > 0.5f);     voiceDirty = true; }   // #221
    else if (suffix == "accentDb")  { r.voiceParams.accentDb        = v;           voiceDirty = true; }
    else if (suffix == "drvChar")    { r.voiceParams.driveChar  = juce::jlimit(0, 12, (int)v); voiceDirty = true; }   // #422/#423
    else if (suffix == "drvDrv")     { r.voiceParams.driveDrive = v;  voiceDirty = true; }
    else if (suffix == "drvOut")     { r.voiceParams.driveOutput= v;  voiceDirty = true; }
    else if (suffix == "drvBits")    { r.voiceParams.drvBits    = v;  voiceDirty = true; }
    else if (suffix == "drvRate")    { r.voiceParams.driveRate  = v;  voiceDirty = true; }
    else if (suffix == "drvDit")     { r.voiceParams.drvDither  = v;  voiceDirty = true; }
    else if (suffix == "drvTon")     { r.voiceParams.driveTone  = v;  voiceDirty = true; }
    else if (suffix == "eqMidGain")  { r.voiceParams.eqMidGain  = v;  voiceDirty = true; }
}

} // namespace mu_pp
