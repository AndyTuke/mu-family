#pragma once

#include <vector>

class EuclideanGenerator
{
public:
    // Distributes `hits` as evenly as possible across `steps`.
    // Returns a bool array of length `steps`. hits is clamped to [0, steps].
    static std::vector<bool> generate(int steps, int hits);
};
