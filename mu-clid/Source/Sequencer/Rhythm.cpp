#include "Rhythm.h"

#include <algorithm>
#include <numeric>

std::vector<bool> Rhythm::getCombinedPattern() const
{
    auto patA = genA.getPattern();
    auto patB = genB.getPattern();

    int lenA = static_cast<int>(patA.size());
    int lenB = static_cast<int>(patB.size());

    if (lenA == 0 && lenB == 0) return {};
    if (lenA == 0) return patB;
    if (lenB == 0) return patA;

    // If a reset point is set, use it as the cycle length.
    // Otherwise span the LCM so the full polyrhythmic cycle is visible.
    int len = resetSteps.has_value()
                  ? std::max(*resetSteps, 1)
                  : std::min(std::lcm(lenA, lenB), 256);

    std::vector<bool> combined(len);
    for (int i = 0; i < len; ++i)
    {
        bool a = patA[i % lenA];
        bool b = patB[i % lenB];

        switch (logic)
        {
            case Logic::OR:    combined[i] = a || b;  break;
            case Logic::AND:   combined[i] = a && b;  break;
            case Logic::XOR:   combined[i] = a != b;  break;
            case Logic::AOnly: combined[i] = a && !b;  break;
            case Logic::BOnly: combined[i] = b && !a;  break;
        }
    }
    return combined;
}

void Rhythm::getCombinedPattern(const EuclidOverrides& ov,
                                std::vector<bool>& out,
                                std::vector<bool>& patA,
                                std::vector<bool>& patB,
                                std::vector<bool>& euclidScratch) const
{
    // Stage B: matches the allocating overload above but writes into pre-reserved
    // buffers. patA / patB / euclidScratch / out must each have capacity ≥ 256 so
    // assign() never reallocates on the audio thread.
    genA.getPattern(ov.a, patA, euclidScratch);
    genB.getPattern(ov.b, patB, euclidScratch);

    const int lenA = static_cast<int>(patA.size());
    const int lenB = static_cast<int>(patB.size());

    if (lenA == 0 && lenB == 0)
    {
        out.clear();
        return;
    }
    if (lenA == 0) { out.assign(patB.begin(), patB.end()); return; }
    if (lenB == 0) { out.assign(patA.begin(), patA.end()); return; }

    const int len = resetSteps.has_value()
                      ? std::max(*resetSteps, 1)
                      : std::min(std::lcm(lenA, lenB), 256);

    out.assign((size_t) len, false);
    for (int i = 0; i < len; ++i)
    {
        const bool a = patA[(size_t) (i % lenA)];
        const bool b = patB[(size_t) (i % lenB)];

        switch (logic)
        {
            case Logic::OR:    out[(size_t) i] = a || b;  break;
            case Logic::AND:   out[(size_t) i] = a && b;  break;
            case Logic::XOR:   out[(size_t) i] = a != b;  break;
            case Logic::AOnly: out[(size_t) i] = a && !b; break;
            case Logic::BOnly: out[(size_t) i] = b && !a; break;
        }
    }
}
