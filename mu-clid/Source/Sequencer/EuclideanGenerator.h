#pragma once

#include <vector>

class EuclideanGenerator
{
public:
    // Distributes `hits` as evenly as possible across `steps`.
    // Returns a bool array of length `steps`. hits is clamped to [0, steps].
    static std::vector<bool> generate(int steps, int hits);

    // Stage B: non-allocating variant. Writes into `out` (must have capacity ≥ steps).
    // Allocation only occurs if `out`'s capacity is insufficient; the audio-thread caller
    // pre-reserves to 256 to guarantee in-place.
    static void generate(int steps, int hits, std::vector<bool>& out);
};
