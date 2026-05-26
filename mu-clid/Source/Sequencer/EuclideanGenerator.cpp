#include "EuclideanGenerator.h"

#include <algorithm>

std::vector<bool> EuclideanGenerator::generate(int steps, int hits)
{
    std::vector<bool> pattern;
    generate(steps, hits, pattern);
    return pattern;
}

void EuclideanGenerator::generate(int steps, int hits, std::vector<bool>& out)
{
    steps = std::max(steps, 1);
    hits  = std::clamp(hits, 0, steps);

    out.assign((size_t)steps, false);
    if (hits == 0)
        return;

    int error = steps - 1;
    for (int i = 0; i < steps; ++i)
    {
        error += hits;
        if (error >= steps)
        {
            error -= steps;
            out[(size_t)i] = true;
        }
    }
}
