#include "HitGenerator.h"

#include <algorithm>
#include <numeric>

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

std::vector<StepType> HitGenerator::getStepTypes() const
{
    if (mute)
        return std::vector<StepType>(steps, StepType::Empty);

    int paddedOut     = prePad + postPad;
    int clampedInsert = (insertMode == InsertMode::Pad) ? insertLength : 0;
    int activeSteps   = std::max(steps - paddedOut - clampedInsert, 0);

    auto boolPat = EuclideanGenerator::generate(activeSteps, hits);

    if (rotate > 0 && activeSteps > 0)
    {
        int r = rotate % activeSteps;
        std::rotate(boolPat.begin(), boolPat.begin() + r, boolPat.end());
    }

    std::vector<StepType> result;
    result.reserve(steps);

    // Pre-pad
    result.insert(result.end(), prePad, StepType::PrePad);

    // Active steps with optional insert
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

    // Post-pad
    result.insert(result.end(), postPad, StepType::PostPad);

    return result;
}
