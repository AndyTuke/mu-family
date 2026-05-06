#include "SequencerEngine.h"

#include <algorithm>

SequencerEngine::SequencerEngine()
{
    lastStepIndex.fill(-1);
}

//==============================================================================
void SequencerEngine::addRhythm(const Rhythm& r)
{
    if ((int)rhythms.size() >= MaxRhythms) return;
    rhythms.push_back(r);
    cachedPatterns.emplace_back();
    cachedCPatterns.emplace_back();
    patternUpdated.push_back(false);
    // safePatterns/safeCPatterns/lastStepIndex are fixed arrays — slot already exists.
    updatePattern((int)rhythms.size() - 1);
}

void SequencerEngine::removeRhythm(int index)
{
    if (index < 0 || index >= (int)rhythms.size()) return;

    bool expected = false;
    while (!patternLock.compare_exchange_weak(expected, true, std::memory_order_acquire))
        expected = false;

    rhythms.erase        (rhythms.begin()         + index);
    cachedPatterns.erase (cachedPatterns.begin()  + index);
    cachedCPatterns.erase(cachedCPatterns.begin() + index);
    patternUpdated.erase (patternUpdated.begin()  + index);

    // Fixed arrays: shift elements left to fill the gap, clear the vacated tail slot.
    const int newN = (int)rhythms.size();
    for (int i = index; i < newN; ++i)
    {
        safePatterns[i]  = std::move(safePatterns[i + 1]);
        safeCPatterns[i] = std::move(safeCPatterns[i + 1]);
        lastStepIndex[i] = lastStepIndex[i + 1];
    }
    safePatterns[newN].clear();
    safeCPatterns[newN].clear();
    lastStepIndex[newN] = -1;

    patternLock.store(false, std::memory_order_release);
}

Rhythm& SequencerEngine::getRhythm(int index)
{
    jassert(index >= 0 && index < (int)rhythms.size());
    return rhythms[index];
}

void SequencerEngine::setNumRhythms(int n)
{
    n = juce::jlimit(0, MaxRhythms, n);
    const int current = (int)rhythms.size();

    if (n > current)
    {
        for (int i = current; i < n; ++i)
        {
            rhythms.emplace_back();
            cachedPatterns.emplace_back();
            cachedCPatterns.emplace_back();
            patternUpdated.push_back(false);
            // safePatterns/safeCPatterns/lastStepIndex: fixed arrays, slots already exist.
        }
    }
    else if (n < current)
    {
        rhythms.resize(n);
        cachedPatterns.resize(n);
        cachedCPatterns.resize(n);
        patternUpdated.resize(n);
        // Clear the fixed array slots that are no longer active.
        for (int i = n; i < current; ++i)
        {
            safePatterns[i].clear();
            safeCPatterns[i].clear();
            lastStepIndex[i] = -1;
        }
    }
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
    const int numRhythms = (int)rhythms.size();
    if (numRhythms == 0)
        return {};

    BlockResult result;

    const auto globalStep    = static_cast<int>(beatPosition / StepLengthBeats);
    const int  effectiveStep = (masterLoopSteps > 0) ? (globalStep % masterLoopSteps) : globalStep;
    masterLoopCurrentStep.store(masterLoopSteps > 0 ? effectiveStep : 0, std::memory_order_relaxed);

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
