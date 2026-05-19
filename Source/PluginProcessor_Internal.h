#pragma once

// #365: shared helpers used by the partial-class TUs that PluginProcessor.cpp was
// split into (PluginProcessor.cpp + PluginProcessor_APVTS.cpp + PluginProcessor_Preset.cpp).
// All members are inline so each TU sees the same definition without an ODR violation.
// Private to the PluginProcessor implementation — do not include from other code.

#include <juce_core/juce_core.h>
#include "Sequencer/Rhythm.h"
#include "ScopedApvtsLoading.h"   // #409: lifted to mu-core — reuse from there.
#include "RhythmParamTable.h"     // #434: declarative per-rhythm param def table.
#include "Audio/AlgorithmNames.h" // #451: kEffectAlgorithmNames / kReverbAlgorithmNames / kInsertAlgorithmNames

namespace mu_pp {

// ── ADSR display-scale converters ────────────────────────────────────────────
// Convert 0–100 UI scale → 0..3 s for ADSR time params.
inline float adsrTime(float v) noexcept { return juce::jmax(0.001f, v * 0.03f); }
// Convert 0–100 UI scale → 0–1 amplitude for ADSR Sustain.
inline float adsrSus (float v) noexcept { return juce::jlimit(0.0f, 1.0f, v / 100.0f); }

// #434: per-rhythm suffix list lives in kRhythmParamDefs (RhythmParamTable.h).
// Iterate that directly instead of a parallel string array. Callers that need
// to enumerate suffixes use a range-for / index loop over kRhythmParamDefs[i].suffix.

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

// #451: per-global-param kind tags + algorithm-name pointers, mirroring
// `kRhythmParamDefs`. Global params don't have apply/push lambdas (they
// flow directly through APVTS rather than via a Rhythm struct), so the
// table is just (id, kind, algorithmNames). Only the four algorithm-selector
// params need AlgorithmIndex; everything else gets the conservative Float
// default (writes the actual de-normalised value, range-stable but not
// reorder-safe).
struct GlobalParamDef
{
    const char*        id;
    ParamKind          kind            = ParamKind::Float;
    const char* const* algorithmNames  = nullptr;
};

inline const GlobalParamDef kGlobalParamDefs[] = {
    // ── Effect slot ──────────────────────────────────────────────────────
    { "eff_algo",      ParamKind::AlgorithmIndex, mu_audio::kEffectAlgorithmNames },
    { "eff_en",        ParamKind::Bool   },
    { "eff_send" },     { "eff_p0" }, { "eff_p1" }, { "eff_p2" }, { "eff_p3" }, { "eff_p4" },
    // ── Delay slot ───────────────────────────────────────────────────────
    { "dly_en",        ParamKind::Bool   },
    { "dly_mode",      ParamKind::Bool   },                    // sync vs free
    { "dly_ms" },
    { "dly_syncDenom", ParamKind::Int    },
    { "dly_syncDot",   ParamKind::Bool   },
    { "dly_syncTrip",  ParamKind::Bool   },
    { "dly_count",     ParamKind::Int    },
    { "dly_fb" }, { "dly_spread" }, { "dly_dirt" }, { "dly_send" },
    // ── Reverb slot ──────────────────────────────────────────────────────
    { "rev_algo",      ParamKind::AlgorithmIndex, mu_audio::kReverbAlgorithmNames },
    { "rev_en",        ParamKind::Bool   },
    { "rev_lvl" }, { "rev_size" }, { "rev_pre" }, { "rev_diff" },
    { "rev_damp" }, { "rev_mod" }, { "rev_dirt" },
    // ── Intra-FX routing ────────────────────────────────────────────────
    { "eff2dly" }, { "eff2rev" }, { "dly2rev" },
    // ── Echo ─────────────────────────────────────────────────────────────
    { "echo_en",       ParamKind::Bool   },
    { "echo_mode" },
    { "echo_ms" },
    { "echo_syncDenom",ParamKind::Int    },
    { "echo_syncDot",  ParamKind::Bool   },
    { "echo_syncTrip", ParamKind::Bool   },
    { "echo_count",    ParamKind::Int    },
    { "echo_fb" }, { "echo_spread" }, { "echo_dirt" },
    // ── Effect return ───────────────────────────────────────────────────
    { "ret_eff_lvl" }, { "ret_eff_pan" },
    { "ret_eff_mute",  ParamKind::Bool   },
    { "ret_eff_solo",  ParamKind::Bool   },
    { "ret_eff_scSrc", ParamKind::Int    },
    { "ret_eff_scAmt" }, { "ret_eff_scAtk" }, { "ret_eff_scRel" },
    // ── Delay return ────────────────────────────────────────────────────
    { "ret_dly_lvl" }, { "ret_dly_pan" },
    { "ret_dly_mute",  ParamKind::Bool   },
    { "ret_dly_solo",  ParamKind::Bool   },
    { "ret_dly_scSrc", ParamKind::Int    },
    { "ret_dly_scAmt" }, { "ret_dly_scAtk" }, { "ret_dly_scRel" },
    // ── Reverb return ───────────────────────────────────────────────────
    { "ret_rev_lvl" }, { "ret_rev_pan" },
    { "ret_rev_mute",  ParamKind::Bool   },
    { "ret_rev_solo",  ParamKind::Bool   },
    { "ret_rev_scSrc", ParamKind::Int    },
    { "ret_rev_scAmt" }, { "ret_rev_scAtk" }, { "ret_rev_scRel" },
    // ── Master ──────────────────────────────────────────────────────────
    { "mstr_lvl" }, { "mstr_pan" },
    { "mstrLoop",      ParamKind::Int    },
    // ── Master insert 1 ─────────────────────────────────────────────────
    { "mst_insChar",   ParamKind::AlgorithmIndex, mu_audio::kInsertAlgorithmNames },
    { "mst_insDrv" }, { "mst_insOut" }, { "mst_insBits" },
    { "mst_insRate" }, { "mst_insDit" }, { "mst_insTon" }, { "mst_insMid" },
    // ── Master insert 2 ─────────────────────────────────────────────────
    { "mst_ins2Char",  ParamKind::AlgorithmIndex, mu_audio::kInsertAlgorithmNames },
    { "mst_ins2Drv" }, { "mst_ins2Out" }, { "mst_ins2Bits" },
    { "mst_ins2Rate" }, { "mst_ins2Dit" }, { "mst_ins2Ton" }, { "mst_ins2Mid" },
};

inline constexpr int kGlobalParamDefCount = (int)(sizeof(kGlobalParamDefs) / sizeof(kGlobalParamDefs[0]));

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

// Current preset-file schema version.
//   v0 (no presetVersion):    legacy pre-#430. Normalised values, drvChar / drvBits
//                              ranges pre-#422/#423 (0..10 / 1..16). Loader applies
//                              migrateLegacyPresetNorm + migrateLegacyGlobalNorm.
//   v1:                       normalised values, post-#422/#423 ranges. No value
//                              shift needed; loader just feeds normalised back.
//   v2 (Stage 35):            actual de-normalised values + string algorithm names.
//                              `r0_stepsA="16"`, `r0_drvChar="Bitcrusher"`,
//                              `r0_aEnvLeg="true"`. Range-widening + algorithm-
//                              reordering safe.
//
// Bump whenever a parameter range / encoding change would invalidate older
// preset files. Files without this property are treated as v0.
inline constexpr int kCurrentPresetVersion = 2;

// #434: now a table lookup. The body that used to live here is in
// RhythmParamTable.h (one entry per suffix; apply + push lambdas co-located).
inline void applyRhythmSuffix(const juce::String& suffix, float v, Rhythm& r,
                               bool& patternDirty, bool& voiceDirty)
{
    if (const auto* def = findRhythmParamDef(suffix))
        def->apply(v, r, patternDirty, voiceDirty);
}

} // namespace mu_pp
