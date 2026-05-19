#pragma once

#include "EuclideanGenerator.h"
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
    // `out`, using `scratch` for the euclidean working buffer. The `ov` argument
    // replaces hits/rotate/prePad/postPad/insertStart/insertLength on this generator
    // (member values untouched). `steps`, `mute`, and the three pad-mode flags stay on
    // the generator. Both buffers must be pre-reserved to ≥ steps capacity for fully
    // allocation-free operation on the audio thread.
    void getPattern(const EuclidGenOverrides& ov,
                    std::vector<bool>& out,
                    std::vector<bool>& scratch) const;
};
