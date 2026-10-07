#pragma once

#include "EuclideanGenerator.h"
#include <algorithm>
#include <cstdint>   // uint8_t (StepType underlying type) — not transitively provided off-MSVC
#include <utility>
#include <vector>

enum class InsertMode { Pad, Mute };

// Per-step type for the ring display, distinguishing hits from pad types.
enum class StepType : uint8_t { Empty = 0, Hit = 1, PrePad = 2, PostPad = 3, InsertPad = 4 };

// per-generator modulated euclid pattern overrides. Replaces the matching
// HitGenerator fields during audio-thread pattern recompute. Step count, mute,
// and pad-mode flags stay on the rhythm — only the integer position params here
// participate in modulation.
struct EuclidGenOverrides
{
    int hits         = 0;
    int rotate       = 0;
    int prePad       = 0;
    int postPad      = 0;
    int insertStart  = 0;
    int insertLength = 0;
    bool operator==(const EuclidGenOverrides& o) const noexcept
    { return hits == o.hits && rotate == o.rotate && prePad == o.prePad
          && postPad == o.postPad && insertStart == o.insertStart
          && insertLength == o.insertLength; }
    bool operator!=(const EuclidGenOverrides& o) const noexcept { return !(*this == o); }
};

class HitGenerator
{
public:
    // Parameter maxima (the APVTS ranges are declared from these). Pre / Post Pad go to the
    // largest padding budget (64 steps - 1); the per-rhythm budget below is the real limit.
    static constexpr int kMaxSteps        = 64;
    static constexpr int kMaxPrePad       = kMaxSteps - 1;
    static constexpr int kMaxPostPad      = kMaxSteps - 1;
    static constexpr int kMaxInsertLength = 8;

    // Padding layout rules: Pre Pad + Post Pad + Insert Length leave at least one step
    // for the Euclid pattern, and the insert sits between the pre and post pads.
    static int maxPadding(int steps) noexcept { return std::max(steps - 1, 0); }

    // Allowed Insert Start range. Insert Start indexes the Euclid section: with a
    // Pad-mode pre-pad that section already begins after the gap, while a Mute-mode
    // pre-pad overlays it, so the gap has to be stepped over explicitly.
    static std::pair<int, int> insertStartBounds(int steps, int pre, int post, int len,
                                                 InsertMode preMode) noexcept
    {
        const int lo = (preMode == InsertMode::Mute) ? pre : 0;
        const int hi = steps - post - len - (preMode == InsertMode::Pad ? pre : 0);
        return { lo, std::max(lo, hi) };
    }

    // Clamp pad / insert values to the layout rules above, in priority order
    // pre -> post -> insert length, then Insert Start into its bounds.
    EuclidGenOverrides clampLayout(EuclidGenOverrides ov) const noexcept
    {
        const int budget = maxPadding(steps);
        ov.prePad       = std::clamp(ov.prePad,       0, std::min(kMaxPrePad,       budget));
        ov.postPad      = std::clamp(ov.postPad,      0, std::min(kMaxPostPad,      budget - ov.prePad));
        ov.insertLength = std::clamp(ov.insertLength, 0, std::min(kMaxInsertLength, budget - ov.prePad - ov.postPad));
        const auto [lo, hi] = insertStartBounds(steps, ov.prePad, ov.postPad, ov.insertLength, prePadMode);
        ov.insertStart  = std::clamp(ov.insertStart, lo, hi);
        return ov;
    }

    // How far each pad knob can turn with the other two where they are — the knob ranges,
    // and the range Pre / Post Pad and Insert Length modulation depth is measured against.
    struct PadKnobMaxima { int prePad = 0, postPad = 0, insertLength = 0; };
    PadKnobMaxima padKnobMaxima() const noexcept
    {
        const int  budget = maxPadding(steps);
        const auto lay    = clampLayout({ hits, rotate, prePad, postPad, insertStart, insertLength });
        return { std::max(0, std::min(kMaxPrePad,       budget - lay.postPad - lay.insertLength)),
                 std::max(0, std::min(kMaxPostPad,      budget - lay.prePad  - lay.insertLength)),
                 std::max(0, std::min(kMaxInsertLength, budget - lay.prePad  - lay.postPad)) };
    }

    int        steps        = 8;
    int        hits         = 0;
    int        rotate       = 0;
    int        prePad       = 0;
    int        postPad      = 0;
    int        insertStart  = 0;
    int        insertLength = 0;
    InsertMode prePadMode   = InsertMode::Pad;
    InsertMode postPadMode  = InsertMode::Pad;
    InsertMode insertMode   = InsertMode::Pad;
    bool       mute         = false;

    // Returns the final bool pattern after euclidean distribution, rotation, padding, and mute.
    std::vector<bool> getPattern() const;

    // Same as getPattern() but annotates each step with its type (hit, empty, pre/post/insert pad).
    std::vector<StepType> getStepTypes() const;
    // Override-aware variant — uses requested hits/rotate/prePad/postPad/insertStart/insertLength
    // (clamped to the layout rules) instead of the member values, so the UI can render the
    // visually-modulated pattern.
    std::vector<StepType> getStepTypes(const EuclidGenOverrides& requested) const;

    // compact POD snapshot of every field that affects getPattern / getStepTypes
    // output. UI consumers (SidebarItem, RhythmCircle) poll this on a timer to detect
    // pattern changes without paying the cost of fetching + comparing the vector
    // representation every tick.
    struct Signature
    {
        int     steps, hits, rotate, prePad, postPad, insertStart, insertLength;
        uint8_t prePadMode, postPadMode, insertMode;
        bool    mute;

        bool operator==(const Signature& o) const noexcept
        {
            return steps == o.steps && hits == o.hits && rotate == o.rotate
                && prePad == o.prePad && postPad == o.postPad
                && insertStart == o.insertStart && insertLength == o.insertLength
                && prePadMode == o.prePadMode && postPadMode == o.postPadMode
                && insertMode == o.insertMode && mute == o.mute;
        }
        bool operator!=(const Signature& o) const noexcept { return !(*this == o); }
    };

    Signature signature() const noexcept
    {
        return { steps, hits, rotate, prePad, postPad, insertStart, insertLength,
                 (uint8_t) prePadMode, (uint8_t) postPadMode, (uint8_t) insertMode, mute };
    }

    // Stage B: non-allocating + override-aware variant. Writes the pattern into
    // `out`, using `scratch` for the euclidean working buffer. The `requested` values
    // (clamped to the layout rules) replace hits/rotate/prePad/postPad/insertStart/insertLength on this generator
    // (member values untouched). `steps`, `mute`, and the three pad-mode flags stay on
    // the generator. Both buffers must be pre-reserved to ≥ steps capacity for fully
    // allocation-free operation on the audio thread.
    void getPattern(const EuclidGenOverrides& requested,
                    std::vector<bool>& out,
                    std::vector<bool>& scratch) const;
};
