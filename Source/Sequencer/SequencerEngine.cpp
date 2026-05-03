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
    jassert(index >= 0 && index < MaxRhythms);
    return rhythms[index];
}

void SequencerEngine::updatePattern(int index)
{
    cachedPatterns[index] = rhythms[index].getCombinedPattern();
    patternUpdated[index] = true;
    // lastStepIndex preserved — processBlock will absorb the current step without firing
}

//==============================================================================
int SequencerEngine::processBlock(double beatPosition)
{
    if (numRhythms == 0)
        return 0;

    int firedMask = 0;

    const auto globalStep    = static_cast<int>(beatPosition / StepLengthBeats);
    const int  effectiveStep = (masterLoopSteps > 0) ? (globalStep % masterLoopSteps) : globalStep;

    for (int r = 0; r < numRhythms; ++r)
    {
        const auto& pattern = cachedPatterns[r];
        if (pattern.empty()) continue;

        const int patLen  = static_cast<int>(pattern.size());
        int stepIndex     = effectiveStep % patLen;

        if (patternUpdated[r])
        {
            patternUpdated[r] = false;
            lastStepIndex[r]  = stepIndex;  // absorb current step — don't fire
        }
        else if (stepIndex != lastStepIndex[r])
        {
            lastStepIndex[r] = stepIndex;
            if (pattern[stepIndex])
                firedMask |= (1 << r);
        }
    }

    return firedMask;
}
