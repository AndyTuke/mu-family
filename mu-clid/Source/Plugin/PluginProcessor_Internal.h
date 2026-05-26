#pragma once

// shared helpers used by the partial-class TUs that PluginProcessor.cpp was
// split into (PluginProcessor.cpp + PluginProcessor_APVTS.cpp + PluginProcessor_Preset.cpp).
// All members are inline so each TU sees the same definition without an ODR violation.
// Private to the PluginProcessor implementation — do not include from other code.

#include <juce_core/juce_core.h>
#include "Sequencer/Rhythm.h"
#include "Persistence/ScopedApvtsLoading.h"   // lifted to mu-core — reuse from there.
#include "Persistence/RhythmParamTable.h"     // declarative per-rhythm param def table.
#include "Audio/AlgorithmNames.h" // kEffectAlgorithmNames / kReverbAlgorithmNames / kInsertAlgorithmNames
#include "Persistence/PresetHelpers.h"        // writeKindedProperty, readKindedPropertyAsActualV2, GlobalParamDef, kGlobalParamDefs

namespace mu_pp {

// ── ADSR display-scale converters ────────────────────────────────────────────
// Convert 0–100 UI scale → 0..3 s for ADSR time params.
inline float adsrTime(float v) noexcept { return juce::jmax(0.001f, v * 0.03f); }
// Convert 0–100 UI scale → 0–1 amplitude for ADSR Sustain.
inline float adsrSus (float v) noexcept { return juce::jlimit(0.0f, 1.0f, v / 100.0f); }

// per-rhythm suffix list lives in kRhythmParamDefs (RhythmParamTable.h).
// Iterate that directly instead of a parallel string array. Callers that need
// to enumerate suffixes use a range-for / index loop over kRhythmParamDefs[i].suffix.

// Per-channel mixer APVTS parameter suffixes (prefix: "ch{i}_").
// Saved in rhythm presets so sends/sidechain travel with the rhythm.
inline const char* const kChannelSuffixes[] = {
    "lvl","pan","mute","solo","sendEff","sendDly","sendRev",
    "scSrc","scAmt","scAtk","scRel","outBus",
    nullptr
};

// Global APVTS parameter IDs are now declared once, in kGlobalParamDefs
// (PresetHelpers.h). Previously a parallel null-terminated `kGlobalParams[]`
// lived here; the only consumer was a self-check test that asserted the two
// tables matched — the test guarded against drift between two copies of the
// same list, not against any real bug. Both are gone; iterate kGlobalParamDefs
// when you need the IDs.

// GlobalParamDef, kGlobalParamDefs, kGlobalParamDefCount moved to PresetHelpers.h
// so the test suite can access them without the PluginProcessor include chain.

// Applies a rhythm parameter suffix + display-scale value to a Rhythm struct.
// Returns patternDirty (true) or voiceDirty (false) via the out-params.
// Called from syncRhythmParam (APVTS TU) and stageRhythmPreset (Preset TU).

// Current preset-file schema version. v2 (Stage 35) is the only format the
// loader accepts: actual de-normalised values + string algorithm names
// (`r0_stepsA="16"`, `r0_drvChar="Bitcrusher"`, `r0_patLeg="true"`).
//
// Legacy formats (v0 / v1 — normalised values, pre-Stage-35) are REFUSED by
// `requireSupportedPresetVersion` at the load entry point. Andy hand-converts
// any old presets via the dev rather than carrying compat code forever.
//
// Bump whenever a parameter range / encoding change would invalidate older
// preset files. The previous compat helpers (`migrateLegacyPresetNorm`,
// `migrateLegacyGlobalNorm`) and the v0/v1 reader branches were removed once
// it became clear there were too few legacy presets to justify the maintenance.
inline constexpr int kCurrentPresetVersion = 2;

// now a table lookup. The body that used to live here is in
// RhythmParamTable.h (one entry per suffix; apply + push lambdas co-located).
inline void applyRhythmSuffix(const juce::String& suffix, float v, Rhythm& r,
                               bool& patternDirty, bool& voiceDirty)
{
    if (const auto* def = findRhythmParamDef(suffix))
        def->apply(v, r, patternDirty, voiceDirty);
}

} // namespace mu_pp
