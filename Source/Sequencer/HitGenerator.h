#pragma once

#include "EuclideanGenerator.h"

enum class InsertMode { Pad, Mute };

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
};
