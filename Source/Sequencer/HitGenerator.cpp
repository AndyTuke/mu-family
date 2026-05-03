#include "HitGenerator.h"

#include <algorithm>
#include <numeric>

std::vector<bool> HitGenerator::getPattern() const
{
    if (mute)
        return std::vector<bool>(steps, false);

    // Pad mode reserves steps from the euclidean distribution; Mute mode does not.
    const int preReserve  = (prePadMode  == InsertMode::Pad) ? prePad  : 0;
    const int postReserve = (postPadMode == InsertMode::Pad) ? postPad : 0;
    const int clampedInsert = (insertMode == InsertMode::Pad) ? insertLength : 0;
    int activeSteps = std::max(steps - preReserve - postReserve - clampedInsert, 0);

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
            pattern.insert(pattern.begin() + clampedStart, insertLength, false);
        }
        else
        {
            int zoneEnd = std::min(clampedStart + insertLength, activeSteps);
            for (int i = clampedStart; i < zoneEnd; ++i)
                pattern[i] = false;
        }
    }

    // Pre-pad: Pad mode inserts silent steps (extends pattern); Mute mode silences the
    // first prePad hits that euclidean placed in that zone.
    if (prePadMode == InsertMode::Pad)
    {
        pattern.insert(pattern.begin(), prePad, false);
    }
    else
    {
        const int zone = std::min(prePad, (int)pattern.size());
        for (int i = 0; i < zone; ++i)
            pattern[i] = false;
    }

    // Post-pad: same distinction.
    if (postPadMode == InsertMode::Pad)
    {
        pattern.insert(pattern.end(), postPad, false);
    }
    else
    {
        const int zone  = std::min(postPad, (int)pattern.size());
        const int start = (int)pattern.size() - zone;
        for (int i = start; i < (int)pattern.size(); ++i)
            pattern[i] = false;
    }

    return pattern;
}

std::vector<StepType> HitGenerator::getStepTypes() const
{
    if (mute)
        return std::vector<StepType>(steps, StepType::Empty);

    const int preReserve  = (prePadMode  == InsertMode::Pad) ? prePad  : 0;
    const int postReserve = (postPadMode == InsertMode::Pad) ? postPad : 0;
    const int clampedInsert = (insertMode == InsertMode::Pad) ? insertLength : 0;
    int activeSteps = std::max(steps - preReserve - postReserve - clampedInsert, 0);

    auto boolPat = EuclideanGenerator::generate(activeSteps, hits);

    if (rotate > 0 && activeSteps > 0)
    {
        int r = rotate % activeSteps;
        std::rotate(boolPat.begin(), boolPat.begin() + r, boolPat.end());
    }

    std::vector<StepType> result;
    result.reserve(steps);

    // Pre-pad in Pad mode: explicit silent steps before the active zone.
    if (prePadMode == InsertMode::Pad)
        result.insert(result.end(), prePad, StepType::PrePad);

    // Active steps (with insert zone if applicable).
    if (insertLength > 0 && insertMode == InsertMode::Pad)
    {
        int clampedStart = std::clamp(insertStart, 0, activeSteps);
        for (int i = 0; i < clampedStart; ++i)
            result.push_back(boolPat[i] ? StepType::Hit : StepType::Empty);
        result.insert(result.end(), insertLength, StepType::InsertPad);
        for (int i = clampedStart; i < activeSteps; ++i)
            result.push_back(boolPat[i] ? StepType::Hit : StepType::Empty);
    }
    else if (insertLength > 0 && insertMode == InsertMode::Mute)
    {
        int clampedStart = std::clamp(insertStart, 0, activeSteps);
        int zoneEnd = std::min(clampedStart + insertLength, activeSteps);
        for (int i = 0; i < activeSteps; ++i)
            result.push_back((i >= clampedStart && i < zoneEnd) ? StepType::Empty
                                                                 : (boolPat[i] ? StepType::Hit : StepType::Empty));
    }
    else
    {
        for (auto b : boolPat)
            result.push_back(b ? StepType::Hit : StepType::Empty);
    }

    // Post-pad in Pad mode: explicit silent steps after the active zone.
    if (postPadMode == InsertMode::Pad)
        result.insert(result.end(), postPad, StepType::PostPad);

    // Mute mode pre/post: the hits in those zones are already silenced in getPattern();
    // overwrite the step types so the ring shows the pad colour for those positions.
    if (prePadMode == InsertMode::Mute && prePad > 0)
    {
        const int zone = std::min(prePad, (int)result.size());
        for (int i = 0; i < zone; ++i)
            result[i] = StepType::PrePad;
    }
    if (postPadMode == InsertMode::Mute && postPad > 0)
    {
        const int zone  = std::min(postPad, (int)result.size());
        const int start = (int)result.size() - zone;
        for (int i = start; i < (int)result.size(); ++i)
            result[i] = StepType::PostPad;
    }

    return result;
}
