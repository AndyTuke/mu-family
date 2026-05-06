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

    bool expected = false;
    while (!patternLock.compare_exchange_weak(expected, true, std::memory_order_acquire))
        expected = false;

    for (int i = index; i < numRhythms - 1; ++i)
    {
        rhythms[i]          = rhythms[i + 1];
        cachedPatterns[i]   = cachedPatterns[i + 1];
        cachedCPatterns[i]  = cachedCPatterns[i + 1];
        lastStepIndex[i]    = lastStepIndex[i + 1];
        patternUpdated[i]   = true;  // audio thread will re-snapshot shifted slot
    }
    --numRhythms;
    lastStepIndex[numRhythms] = -1;
    cachedPatterns[numRhythms].clear();
    cachedCPatterns[numRhythms].clear();
    patternUpdated[numRhythms] = false;

    patternLock.store(false, std::memory_order_release);
}

Rhythm& SequencerEngine::getRhythm(int index)
{
    jassert(index >= 0 && index < MaxRhythms);
    return rhythms[index];
}

void SequencerEngine::updatePattern(int index)
{
    bool expected = false;
    while (!patternLock.compare_exchange_weak(expected, true, std::memory_order_acquire))
        expected = false;

    cachedPatterns[index]  = rhythms[index].getCombinedPattern();
    cachedCPatterns[index] = rhythms[index].genC.getPattern();
    patternUpdated[index]  = true;
    // lastStepIndex preserved — processBlock will absorb the current step without firing

    patternLock.store(false, std::memory_order_release);
}

//==============================================================================
BlockResult SequencerEngine::processBlock(double beatPosition)
{
    if (numRhythms == 0)
        return {};

    BlockResult result;

    const auto globalStep    = static_cast<int>(beatPosition / StepLengthBeats);
    const int  effectiveStep = (masterLoopSteps > 0) ? (globalStep % masterLoopSteps) : globalStep;

    // Non-blocking snapshot: copy any updated patterns into the audio-thread-safe buffers.
    // If patternLock is held by the message thread, skip this block's snapshot — safePatterns
    // from the previous block will be used, avoiding any blocking or data race.
    {
        bool expected = false;
        if (patternLock.compare_exchange_strong(expected, true, std::memory_order_acquire))
        {
            for (int r = 0; r < numRhythms; ++r)
            {
                if (patternUpdated[r])
                {
                    safePatterns[r]   = cachedPatterns[r];
                    safeCPatterns[r]  = cachedCPatterns[r];
                    patternUpdated[r] = false;
                    // Absorb the current step so we don't retroactively fire a hit.
                    if (!safePatterns[r].empty())
                        lastStepIndex[r] = effectiveStep % (int)safePatterns[r].size();
                }
            }
            patternLock.store(false, std::memory_order_release);
        }
    }

    for (int r = 0; r < numRhythms; ++r)
    {
        const auto& pattern = safePatterns[r];
        if (pattern.empty()) continue;

        const int patLen  = static_cast<int>(pattern.size());
        int stepIndex     = effectiveStep % patLen;

        if (stepIndex != lastStepIndex[r])
        {
            lastStepIndex[r] = stepIndex;
            if (pattern[stepIndex])
            {
                result.firedMask |= (1 << r);

                // Check Ring C coincidence for accent detection.
                const auto& cPat = safeCPatterns[r];
                if (!cPat.empty())
                {
                    const int cStep = effectiveStep % static_cast<int>(cPat.size());
                    if (cPat[cStep])
                        result.accentMask |= (1 << r);
                }
            }
        }
    }

    return result;
}
