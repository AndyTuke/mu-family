#pragma once

#include <juce_data_structures/juce_data_structures.h>

// Format-migration helpers extracted from PresetIO.cpp (#664). Each one
// rewrites an older on-disk preset / host-state encoding into the current
// schema, in place on a parsed ValueTree, before the load path reads
// parameters back out of it.
//
// They run unconditionally on every load — preset-version gating was removed
// in #643 (pre-distribution era). A tree already in the current format is
// detected and left untouched by each function's early-out, so re-running is a
// no-op.
namespace mu_pp_migrate
{

// v3 per-rhythm insert: collapse the 9 named drive/EQ fields (drvDrv / drvOut /
// drvDit / drvTon / drvBits / drvRate / eqLowGain / eqMidGain / eqHighGain) to
// 4 generic normalised slot params (insP1..insP4), per-algo. Also folds the
// pre-#597 packed-EQ encoding (Low/High in drvDrv/drvDit as 0..100) into the EQ
// algo's slots. `srcPrefix` is "r0_" for rhythm presets / hot-swap loads, or ""
// for a MuClidPreset per-rhythm Rhythm child tree.
void migrateInsertSlotsV3(juce::ValueTree& tree, const juce::String& srcPrefix);

// As above for a master insert slot ({1, 2}) inside a GlobalState child
// (mst_ins* / mst_ins2* property names).
void migrateMasterInsertSlotsV3(juce::ValueTree& tree, int slot);

// Remap old modulator destination IDs (e.g. "insert.drive", "ks.note",
// "voc.octave") to the new "insert.p1".."insert.p4" slots, per-algo. `algoIdx`
// is the rhythm's resolved insert algorithm; assignments with no slot in the
// new layout are dropped.
void migrateModAssignmentsV3(juce::ValueTree& modsTree, int algoIdx);

} // namespace mu_pp_migrate
