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

// FX algorithm IDs mirror FXAlgorithmRegistry::effectAlgorithms()[i].id /
// reverbAlgorithms()[i].id. Duplicated here so non-UI code (preset save /
// load) can consume the names without including the heavier registry header.
// MUST stay in lock-step with the registry — adding a new effect there
// requires appending an entry here too.

// eff_algo 0..3 (chorus/flanger/phaser/echo). Registry currently caps at 4
// entries despite the APVTS range 0..7 (unused slots reserved for v2 plugins).
inline const char* const kEffectAlgorithmNames[] = {
    "chorus",       // 0
    "flanger",      // 1
    "phaser",       // 2
    "echo",         // 3
    nullptr
};

// rev_algo 0..3 (room/hall/plate/spring).
inline const char* const kReverbAlgorithmNames[] = {
    "room",         // 0
    "hall",         // 1
    "plate",        // 2
    "spring",       // 3
    nullptr
};

// modulator enum names. ControlSequence's `Mode`, `Polarity`, and the
// timing enums `NoteValue` / `NoteMod` live in Source/Sequencer/ControlSequence.h
// and Source/Sequencer/Rhythm.h respectively. They're saved by serialiseModulators
// as raw int indices — same drift hazard as the per-rhythm algorithm selectors
// fixed in Stage 35 step 1, just in the modulator subtree. Name tables here
// drive the v2-name-string serialisation; the deserialiser accepts either a
// name string or an int (legacy compat).

// ControlSequence::Mode { Smooth, Stepped }
inline const char* const kModulatorModeNames[] = {
    "Smooth",       // 0
    "Stepped",      // 1
    nullptr
};

// ControlSequence::Polarity { Unipolar, Bipolar }
inline const char* const kModulatorPolarityNames[] = {
    "Unipolar",     // 0
    "Bipolar",      // 1
    nullptr
};

// NoteValue { Whole, Half, Quarter, Eighth, Sixteenth, ThirtySecond }
inline const char* const kNoteValueNames[] = {
    "Whole",        // 0
    "Half",         // 1
    "Quarter",      // 2
    "Eighth",       // 3
    "Sixteenth",    // 4
    "ThirtySecond", // 5
    nullptr
};

// NoteMod { None, Triplet, Dotted }
inline const char* const kNoteModNames[] = {
    "None",         // 0
    "Triplet",      // 1
    "Dotted",       // 2
    nullptr
};

// Logic { OR, AND, XOR, AOnly, BOnly } — used by `r.logic` per-rhythm.
// Persisted via kRhythmParamDefs as an Int today; Stage 35 follow-up could
// upgrade this entry to AlgorithmIndex pointing at the table below so
// reordering Logic enumerators stays preset-safe.
inline const char* const kLogicNames[] = {
    "OR",           // 0
    "AND",          // 1
    "XOR",          // 2
    "AOnly",        // 3
    "BOnly",        // 4
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
