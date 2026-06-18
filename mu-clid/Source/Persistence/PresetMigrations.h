#pragma once

#include <juce_data_structures/juce_data_structures.h>

// Format-migration helpers extracted from PresetIO.cpp. Each one
// rewrites an older on-disk preset / host-state encoding into the current
// schema, in place on a parsed ValueTree, before the load path reads
// parameters back out of it.
//
// They run unconditionally on every load — preset-version gating was removed
// in the pre-distribution era. A tree already in the current format is
// detected and left untouched by each function's early-out, so re-running is a
// no-op.
namespace mu_pp_migrate
{

// Host-state format version. Bump whenever the on-disk schema changes in a way
// that requires migration on load.
//   v0 / v1 : legacy (pre-v2) — ADSR A/D/R stored as 0..100 (×0.03 → seconds)
//   v2      : current — ADSR A/D/R stored as 0..10 (seconds direct)
inline constexpr int kCurrentStateFormatVersion = 2;

// In-place migration of pre-v2 host state. APVTS stores parameter values as
// <PARAM id="..." value="..."/> children of the root tree. For legacy state
// (formatVersion absent or < 2), the ADSR A/D/R values lived in 0..100; the new
// schema stores them in 0..10 seconds directly. Migration multiplies by 0.03
// (old display→seconds factor), clamps to the new max, and preserves the
// End-mode sentinel on aEnvRel (old slider max 100 → new max 10 = "play to
// natural sample end"). No-op once formatVersion >= kCurrentStateFormatVersion.
void migrateLegacyHostState(juce::ValueTree& state);

// v3 per-rhythm insert: collapse the 9 named drive/EQ fields (drvDrv / drvOut /
// drvDit / drvTon / drvBits / drvRate / eqLowGain / eqMidGain / eqHighGain) to
// 4 generic normalised slot params (insP1..insP4), per-algo. Also folds the
// legacy packed-EQ encoding (Low/High in drvDrv/drvDit as 0..100) into the EQ
// algo's slots. `srcPrefix` is "r0_" for rhythm presets / hot-swap loads, or ""
// for a MuClidPreset per-rhythm Rhythm child tree.
void migrateInsertSlotsV3(juce::ValueTree& tree, const juce::String& srcPrefix);

// As above for a master insert slot ({1, 2}) inside a GlobalState child
// (mst_ins* / mst_ins2* property names).
void migrateMasterInsertSlotsV3(juce::ValueTree& tree, int slot);

// Remap old modulator destination IDs (e.g. "insert.drive", "ks.note",
// "voc.octave") to the new "insert.p1".."insert.p4" slots, per-algo. `algoIndex`
// is the rhythm's resolved insert algorithm; assignments with no slot in the
// new layout are dropped.
void migrateModAssignmentsV3(juce::ValueTree& modsTree, int algoIndex);

} // namespace mu_pp_migrate
