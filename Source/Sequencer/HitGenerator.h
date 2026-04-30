#pragma once

#include "EuclideanGenerator.h"

enum class InsertMode { Pad, Mute };

// Per-step type for the ring display, distinguishing hits from pad types.
enum class StepType : uint8_t { Empty = 0, Hit = 1, PrePad = 2, PostPad = 3, InsertPad = 4 };

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
    InsertMode insertMode   = InsertMode::Pad;
    bool       mute         = false;

    // Returns the final bool pattern after euclidean distribution, rotation, padding, and mute.
    std::vector<bool> getPattern() const;

    // Same as getPattern() but annotates each step with its type (hit, empty, pre/post/insert pad).
    std::vector<StepType> getStepTypes() const;
};
