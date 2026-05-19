#pragma once

#include <juce_core/juce_core.h>
#include <cstring>

// Stage 35 Step 1: stable algorithm-name tables for the v2 preset format.
//
// Preset XML (.muRhyth / .muclid, v2+) saves algorithm-selector parameters as
// string names rather than raw integer indices. That way the user can reorder /
// insert / remove algorithms in the dispatch table without invalidating every
// saved preset — the names stay stable across releases, the indices don't.
//
// Hot rules:
//   - Names are APPEND-ONLY. Adding a new algorithm at the END of the dispatch
//     table is safe — existing preset names resolve to the same algorithms.
//     Inserting in the middle or reordering changes the index but old preset
//     names will still resolve to the right algorithm via the name table.
//   - Names must NEVER change after a public release. A typo fix or rename
//     requires an alias entry pointing the old name at the same index, so
//     existing presets keep loading.
//   - Names are ASCII, no spaces — they end up as XML attribute values and we
//     want them grep-able and hand-edit-able.
//
// FX (Effect / Reverb) algorithms get their stable IDs from
// `FXAlgorithmRegistry::effectAlgorithms()[i].id` / `reverbAlgorithms()[i].id`
// already (see Source/FX/FXAlgorithmDef.h). The tables in this header cover
// the two registries that don't have an equivalent: per-voice insert (driveChar)
// and per-voice filter (filterType).

namespace mu_audio {

// driveChar 0..12. Order MUST match InsertProcessor::dispatch[] in
// Source/Audio/InsertProcessor.cpp — that's the audio-thread lookup. Nullptr-
// terminated for size-introspection.
inline const char* const kInsertAlgorithmNames[] = {
    "None",         // 0
    "SoftClip",     // 1
    "HardClip",     // 2
    "Fold",         // 3
    "Bitcrusher",   // 4
    "Clipper",      // 5
    "EQ",           // 6
    "Compressor",   // 7
    "Limiter",      // 8
    "RingMod",      // 9
    "TapeSat",      // 10
    "Karplus",      // 11    #422
    "Vocoder",      // 12    #423
    nullptr
};

// filterType 0..15. Order MUST match MultiModeFilter::algorithms[] in
// Source/Audio/MultiModeFilter.cpp.
inline const char* const kFilterTypeNames[] = {
    "LP12",         // 0
    "HP12",         // 1
    "BP12",         // 2
    "Notch12",      // 3
    "LP24",         // 4
    "HP24",         // 5
    "BP24",         // 6
    "LP6",          // 7
    "CombPlus",     // 8
    "AP12",         // 9
    "Notch24",      // 10
    "HP6",          // 11
    "Peak",         // 12
    "LowShelf",     // 13
    "HighShelf",    // 14
    "CombMinus",    // 15
    nullptr
};

// Look up algorithm index for a given name. Returns -1 if not found.
// Linear scan — called only from preset save/load paths (interactive), so the
// O(N) cost is irrelevant.
inline int indexFromName(const char* const* table, const juce::String& name) noexcept
{
    if (table == nullptr) return -1;
    for (int i = 0; table[i] != nullptr; ++i)
        if (name == table[i])
            return i;
    return -1;
}

// Look up name for a given index. Returns nullptr if out of range. Caller
// should check and fall back to writing the integer index as a last resort
// (a fresh-from-save preset should never hit this; only would happen if the
// in-memory algorithm field somehow held a value outside the dispatch range).
inline const char* nameFromIndex(const char* const* table, int idx) noexcept
{
    if (table == nullptr || idx < 0) return nullptr;
    for (int i = 0; table[i] != nullptr; ++i)
        if (i == idx) return table[i];
    return nullptr;
}

// Count entries in a null-terminated name table. constexpr-evaluable on the
// inline tables above so the compiler folds size() at compile time.
inline constexpr int countNames(const char* const* table) noexcept
{
    int n = 0;
    while (table[n] != nullptr) ++n;
    return n;
}

} // namespace mu_audio
