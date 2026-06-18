#pragma once

#include <juce_core/juce_core.h>
#include <cmath>
#include <algorithm>

// Per-algorithm, per-slot configuration for the four generic insert "Param N"
// knobs. Single source of truth consumed by:
//   • DSP   — each InsertAlgorithm::process reads `p.insertParam[slot]` (a
//     normalised 0..1 value) and calls `normToActual(norm, algo, slot)` to
//     get the real-world value with the right range + skew applied.
//   • UI    — the per-channel InsertSubsection + the master MixerChannel_Insert
//     iterate the 4 slots, read label/range/skew/unit from here, and wire the
//     knob's `setValue` / `onValueChanged` to convert between the displayed
//     actual range and the normalised APVTS storage via the same helpers.
//   • Preset migration — v2 presets stored 9 named fields (insertDrive /
//     insertOutput / insertDither / insertTone / insertBits / insertRate /
//     insertEqLow/Mid/High) with algorithm-specific per-field interpretation;
//     v3 collapses all of that to `insertParam[4]` normalised 0..1. Migration
//     reads the old field appropriate for the saved algo and re-encodes via
//     `actualToNorm`.
//
// Adding a new algorithm = append one row to `kInsertAlgoSlots` + append to
// kInsertAlgorithmNames + add a new dispatch entry in InsertProcessor. Three
// edits, no UI switch-case to chase.
//
// Field-order rule: row index N must equal InsertAlgorithmNames[N] index.

namespace mu_ui {

inline constexpr int kInsertSlotCount = 4;
// Number of insert algorithms (rows in kInsertAlgoSlots / kInsertAlgoDefaults).
// Must equal InsertProcessor::kNumInsertAlgos.
inline constexpr int kInsertAlgoCount = 14;

// Skew shape between the displayed actual value and the normalised 0..1
// stored in VoiceParams + APVTS. Knobs apply the SAME skew on the slider
// directly so the UI feel matches the underlying storage.
enum class SkewMode { Linear, Log, IntStep };

// Per-slot metadata. `label = nullptr` means the slot is hidden for this
// algorithm. `unitSuffix` drives the standard value-display formatter ("Hz",
// "dB", "ms", "%", "" for raw); algorithms with bespoke string conversions
// (Note names, Waveshape names) use the `enumNames` array instead.
struct SlotConfig
{
    const char*        label        = nullptr;
    float              minVal       = 0.0f;
    float              maxVal       = 0.0f;
    SkewMode           skew         = SkewMode::Linear;
    const char*        unitSuffix   = "";
    // Optional bespoke value-name table — used for Note (12 entries),
    // Waveshape (4 entries), Unison count (7 entries). The displayed value
    // is `enumNames[ round(actual) - enumOffset ]`. nullptr → use unit-based
    // numeric formatter.
    const char* const* enumNames    = nullptr;
    int                enumOffset   = 0;   // subtract from rounded actual to index enumNames
    // For Hz-skewed knobs the label switches Hz↔kHz at 1 kHz; for ms-skewed
    // ones it switches ms↔s at 1 s. `dynamicUnit` enables that — `unitSuffix`
    // becomes the LOW-range unit, and the formatter picks the HIGH unit
    // beyond the threshold (Hz→kHz at 1000; ms→s at 1000).
    bool               dynamicUnit  = false;
};

// Bespoke name tables used by Karplus / Vocoder / VocoderSt slots.
inline const char* const kNoteNames[12]    = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
inline const char* const kWaveNames[4]     = { "Saw","Square","White","Pink" };
inline const char* const kUnisonNames[7]   = { "1","3","5","7","9","11","13" };

// Per-algorithm slot table. 14 rows × 4 cols.
//
// Source order MUST match the index order of InsertProcessor::dispatch[] /
// mu_audio::kInsertAlgorithmNames so a single integer dispatches the
// algorithm's DSP, name, and slot layout.
//
// Invariant: every non-None algorithm (idx >= 1) must have at least ONE slot
// with `label != nullptr`. Otherwise the UI driver hides all four knobs and
// the algorithm presents as empty — the symptom that surfaces when a
// stale algo flows into configureInsertAlgorithm. Enforced by a runtime
// expect inside InsertAlgoTableTests (no constexpr path because the labels
// are non-`constexpr` string-literal pointers on MSVC).
inline const SlotConfig kInsertAlgoSlots[kInsertAlgoCount][kInsertSlotCount] = {
    // ── 0 None ─────────────────────────────────────────────────────────
    { {}, {}, {}, {} },

    // ── 1 SoftClip ── Drive / Output / — / LPF ────────────────────────
    { { "Drive",  0.0f,  100.0f, SkewMode::Linear, "" },
      { "Output", -24.0f, 0.0f,  SkewMode::Linear, "dB" },
      { },
      { "LPF",    20.0f,  20000.0f, SkewMode::Log, "Hz", nullptr, 0, true } },

    // ── 2 HardClip ── same shape as SoftClip ──────────────────────────
    { { "Drive",  0.0f,  100.0f, SkewMode::Linear, "" },
      { "Output", -24.0f, 0.0f,  SkewMode::Linear, "dB" },
      { },
      { "LPF",    20.0f,  20000.0f, SkewMode::Log, "Hz", nullptr, 0, true } },

    // ── 3 Fold ── same shape ──────────────────────────────────────────
    { { "Drive",  0.0f,  100.0f, SkewMode::Linear, "" },
      { "Output", -24.0f, 0.0f,  SkewMode::Linear, "dB" },
      { },
      { "LPF",    20.0f,  20000.0f, SkewMode::Log, "Hz", nullptr, 0, true } },

    // ── 4 Bitcrusher ── Bits / Rate / Dither / LPF ────────────────────
    { { "Bits",   1.0f,   16.0f,    SkewMode::IntStep, "" },
      { "Rate",   100.0f, 48000.0f, SkewMode::Log,     "Hz", nullptr, 0, true },
      { "Dither", 0.0f,   100.0f,   SkewMode::Linear,  "%" },
      { "LPF",    20.0f,  20000.0f, SkewMode::Log,     "Hz", nullptr, 0, true } },

    // ── 5 Clipper ── Threshold / Output / — / LPF ─────────────────────
    { { "Threshold", 0.0f,  100.0f,  SkewMode::Linear, "" },
      { "Output",    -24.0f, 0.0f,   SkewMode::Linear, "dB" },
      { },
      { "LPF",       20.0f,  20000.0f, SkewMode::Log, "Hz", nullptr, 0, true } },

    // ── 6 EQ ── Low / Mid / Mid Hz / High ─────────────────────────────
    { { "Low",    -18.0f, 18.0f,   SkewMode::Linear, "dB" },
      { "Mid",    -18.0f, 18.0f,   SkewMode::Linear, "dB" },
      { "Mid Hz", 200.0f, 8000.0f, SkewMode::Log,    "Hz", nullptr, 0, true },
      { "High",   -18.0f, 18.0f,   SkewMode::Linear, "dB" } },

    // ── 7 Compressor ── Threshold / Output / Attack / Release ─────────
    { { "Threshold", 0.0f,   100.0f,  SkewMode::Linear, "" },
      { "Output",    -24.0f, 24.0f,   SkewMode::Linear, "dB" },
      { "Attack",    0.0f,   100.0f,  SkewMode::Linear, "ms" },
      { "Release",   20.0f,  2000.0f, SkewMode::Log,    "ms", nullptr, 0, true } },

    // ── 8 Limiter ── Ceiling / Output / Attack / Release ──────────────
    { { "Ceiling",   0.0f,   100.0f,  SkewMode::Linear, "" },
      { "Output",    -24.0f, 24.0f,   SkewMode::Linear, "dB" },
      { "Attack",    0.0f,   100.0f,  SkewMode::Linear, "ms" },
      { "Release",   20.0f,  2000.0f, SkewMode::Log,    "ms", nullptr, 0, true } },

    // ── 9 RingMod ── Mix / — / — / Freq ───────────────────────────────
    { { "Mix",  0.0f,  100.0f,  SkewMode::Linear, "%" },
      { },
      { },
      { "Freq", 10.0f, 5000.0f, SkewMode::Log, "Hz", nullptr, 0, true } },

    // ── 10 TapeSat ── Drive / Output / — / Tone ───────────────────────
    { { "Drive",  0.0f,  100.0f, SkewMode::Linear, "" },
      { "Output", -24.0f, 0.0f,  SkewMode::Linear, "dB" },
      { },
      { "Tone",   200.0f, 20000.0f, SkewMode::Log, "Hz", nullptr, 0, true } },

    // ── 11 Karplus ── Note / Octave / Feedback / LPF ──────────────────
    { { "Note",     0.0f,  11.0f,    SkewMode::IntStep, "",  kNoteNames, 0 },
      { "Octave",   0.0f,  3.0f,     SkewMode::IntStep, "" },
      { "Feedback", 0.0f,  100.0f,   SkewMode::Linear,  "%" },
      { "LPF",      20.0f, 20000.0f, SkewMode::Log,     "Hz", nullptr, 0, true } },

    // ── 12 Vocoder ── Wave / Unison / Octave / Note ───────────────────
    { { "Wave",   0.0f, 3.0f,  SkewMode::IntStep, "",  kWaveNames,   0 },
      { "Unison", 0.0f, 6.0f,  SkewMode::IntStep, "",  kUnisonNames, 0 },
      { "Octave", 1.0f, 5.0f,  SkewMode::IntStep, "" },
      { "Note",   1.0f, 12.0f, SkewMode::IntStep, "",  kNoteNames,   1 } },

    // ── 13 VocoderSt ── same as Vocoder ───────────────────────────────
    { { "Wave",   0.0f, 3.0f,  SkewMode::IntStep, "",  kWaveNames,   0 },
      { "Unison", 0.0f, 6.0f,  SkewMode::IntStep, "",  kUnisonNames, 0 },
      { "Octave", 1.0f, 5.0f,  SkewMode::IntStep, "" },
      { "Note",   1.0f, 12.0f, SkewMode::IntStep, "",  kNoteNames,   1 } },
};

// First-visit / cold-restore defaults per algorithm. Indexed [algo][slot],
// stored as ACTUAL (not normalised) values so they read naturally. The init
// path normalises via actualToNorm() before writing to APVTS. Replaces the
// prior `kInsertAlgoDefaults[14]` table that listed 7 named fields per algo.
inline const float kInsertAlgoDefaults[kInsertAlgoCount][kInsertSlotCount] = {
    /* 0 None        */ { 0.0f,    0.0f,    0.0f,   20000.0f },
    /* 1 SoftClip    */ { 0.0f,    0.0f,    0.0f,   20000.0f },
    /* 2 HardClip    */ { 0.0f,    0.0f,    0.0f,   20000.0f },
    /* 3 Fold        */ { 0.0f,    0.0f,    0.0f,   20000.0f },
    /* 4 Bitcrusher  */ { 16.0f,   48000.0f, 0.0f,  20000.0f },
    /* 5 Clipper     */ { 100.0f,  0.0f,    0.0f,   20000.0f },
    /* 6 EQ          */ { 0.0f,    0.0f,    1000.0f, 0.0f    },
    /* 7 Compressor  */ { 30.0f,   0.0f,    5.0f,   200.0f   },
    /* 8 Limiter     */ { 30.0f,   0.0f,    5.0f,   200.0f   },
    /* 9 RingMod     */ { 50.0f,   0.0f,    0.0f,   440.0f   },
    /*10 TapeSat     */ { 0.0f,    0.0f,    0.0f,   20000.0f },
    /*11 Karplus     */ { 0.0f,    1.0f,    0.0f,   20000.0f },
    /*12 Vocoder     */ { 0.0f,    0.0f,    1.0f,   1.0f     },
    /*13 VocoderSt   */ { 0.0f,    0.0f,    1.0f,   1.0f     },
};

// Convert a normalised 0..1 storage value to the algorithm's actual value
// with the slot's skew applied. Safe for any (algo, slot) — hidden slots
// (label == nullptr) simply return the slot's minVal (0).
inline float normToActual(float norm, int algoIndex, int slot) noexcept
{
    if (algoIndex < 0 || algoIndex >= kInsertAlgoCount || slot < 0 || slot >= kInsertSlotCount)
        return 0.0f;
    const auto& cfg = kInsertAlgoSlots[algoIndex][slot];
    norm = std::clamp(norm, 0.0f, 1.0f);
    switch (cfg.skew)
    {
        case SkewMode::Log:
        {
            const float lo = std::max(0.0001f, cfg.minVal);
            return lo * std::pow(cfg.maxVal / lo, norm);
        }
        case SkewMode::IntStep:
        {
            const float v = cfg.minVal + norm * (cfg.maxVal - cfg.minVal);
            return std::round(v);
        }
        case SkewMode::Linear:
        default:
            return cfg.minVal + norm * (cfg.maxVal - cfg.minVal);
    }
}

// Convert an actual value to its 0..1 normalised storage form. Inverse of
// normToActual, with matching skew handling.
inline float actualToNorm(float actual, int algoIndex, int slot) noexcept
{
    if (algoIndex < 0 || algoIndex >= kInsertAlgoCount || slot < 0 || slot >= kInsertSlotCount)
        return 0.0f;
    const auto& cfg = kInsertAlgoSlots[algoIndex][slot];
    actual = std::clamp(actual, cfg.minVal, cfg.maxVal);
    switch (cfg.skew)
    {
        case SkewMode::Log:
        {
            const float lo = std::max(0.0001f, cfg.minVal);
            if (cfg.maxVal <= lo) return 0.0f;
            return std::log(actual / lo) / std::log(cfg.maxVal / lo);
        }
        case SkewMode::IntStep:
        case SkewMode::Linear:
        default:
        {
            const float span = cfg.maxVal - cfg.minVal;
            return span > 0.0f ? (actual - cfg.minVal) / span : 0.0f;
        }
    }
}

} // namespace mu_ui
