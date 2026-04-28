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
            case Logic::AOnly: combined[i] = a;        break;
            case Logic::BOnly: combined[i] = b;        break;
        }
    }
    return combined;
}
