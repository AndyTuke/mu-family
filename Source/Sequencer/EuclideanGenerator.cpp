#include "EuclideanGenerator.h"

#include <algorithm>

std::vector<bool> EuclideanGenerator::generate(int steps, int hits)
{
    steps = std::max(steps, 1);
    hits  = std::clamp(hits, 0, steps);

    std::vector<bool> pattern(steps, false);
    if (hits == 0)
        return pattern;

    int error = steps - 1;
    for (int i = 0; i < steps; ++i)
    {
        error += hits;
        if (error >= steps)
        {
            error -= steps;
            pattern[i] = true;
        }
    }
    return pattern;
}
