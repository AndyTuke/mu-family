#include "HitGenerator.h"

#include <algorithm>

std::vector<bool> HitGenerator::getPattern() const
{
    if (mute)
        return std::vector<bool>(steps, false);

    int paddedOut  = prePad + postPad;
    int clampedInsert = (insertMode == InsertMode::Pad) ? insertLength : 0;
    int activeSteps = std::max(steps - paddedOut - clampedInsert, 0);

    auto pattern = EuclideanGenerator::generate(activeSteps, hits);

    if (rotate > 0 && activeSteps > 0)
    {
        int r = rotate % activeSteps;
        std::rotate(pattern.begin(), pattern.begin() + r, pattern.end());
    }

    if (insertLength > 0)
    {
        int clampedStart = std::clamp(insertStart, 0, activeSteps);

        if (insertMode == InsertMode::Pad)
        {
            // Gap excluded from distribution — insert silent steps into the pattern.
            pattern.insert(pattern.begin() + clampedStart, insertLength, false);
        }
        else
        {
            // Mute mode — hits distributed through the zone but silenced.
            int zoneEnd = std::min(clampedStart + insertLength, activeSteps);
            for (int i = clampedStart; i < zoneEnd; ++i)
                pattern[i] = false;
        }
    }

    pattern.insert(pattern.begin(), prePad, false);
    pattern.insert(pattern.end(),   postPad, false);

    return pattern;
}
