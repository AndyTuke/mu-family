#include "SequencerEngine.h"

#include <algorithm>

SequencerEngine::SequencerEngine()
{
    lastStepIndex.fill(-1);
}

//==============================================================================
void SequencerEngine::addRhythm(const Rhythm& r)
{
    if (numRhythms >= MaxRhythms) return;
    rhythms[numRhythms] = r;
    updatePattern(numRhythms);
    ++numRhythms;
}

void SequencerEngine::removeRhythm(int index)
{
    if (index < 0 || index >= numRhythms) return;
    for (int i = index; i < numRhythms - 1; ++i)
    {
        rhythms[i]        = rhythms[i + 1];
        cachedPatterns[i] = cachedPatterns[i + 1];
        lastStepIndex[i]  = lastStepIndex[i + 1];
    }
    --numRhythms;
    lastStepIndex[numRhythms] = -1;
}

Rhythm& SequencerEngine::getRhythm(int index)
{
    return rhythms[index];
}

void SequencerEngine::updatePattern(int index)
{
    cachedPatterns[index] = rhythms[index].getCombinedPattern();
    lastStepIndex[index]  = -1; // force re-evaluation next block
}

//==============================================================================
int SequencerEngine::processBlock(double beatPosition)
{
    if (numRhythms == 0)
        return 0;

    int firedMask = 0;

    for (int r = 0; r < numRhythms; ++r)
    {
        const auto& pattern = cachedPatterns[r];
        if (pattern.empty()) continue;

        int patLen = static_cast<int>(pattern.size());

        auto globalStep = static_cast<int>(beatPosition / StepLengthBeats);
        int stepIndex   = globalStep % patLen;

        if (stepIndex != lastStepIndex[r])
        {
            lastStepIndex[r] = stepIndex;
            if (pattern[stepIndex])
                firedMask |= (1 << r);
        }
    }

    return firedMask;
}
