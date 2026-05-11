#include "SequencerEngine.h"

#include <algorithm>

SequencerEngine::SequencerEngine()
{
    lastStepIndex.fill(-1);
    rhythms.reserve(MaxRhythms);
    cachedPatterns.reserve(MaxRhythms);
    cachedCPatterns.reserve(MaxRhythms);
    patternUpdated.reserve(MaxRhythms);

    // #234: pre-size every safePatterns / safeCPatterns slot to the worst-case
    // step count (256 — matches the resetSteps cap and the LCM clamp inside
    // getCombinedPattern). Audio-thread copy-assignment from cachedPatterns later
    // is then guaranteed not to allocate, because the destination already has
    // enough capacity. assign() (used in processBlock) overwrites contents in
    // place when capacity is sufficient — the audio-thread no-alloc rule holds.
    static constexpr size_t kMaxStepCount = 256;
    for (int i = 0; i < MaxRhythms; ++i)
    {
        safePatterns [i].reserve(kMaxStepCount);
        safeCPatterns[i].reserve(kMaxStepCount);
    }
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

void SequencerEngine::swapRhythmSlots(int i, int j)
{
    if (i == j) return;
    const int n = (int)rhythms.size();
    if (i < 0 || j < 0 || i >= n || j >= n) return;

    bool expected = false;
    while (!patternLock.compare_exchange_weak(expected, true, std::memory_order_acquire))
        expected = false;

    std::swap(rhythms[i],         rhythms[j]);
    std::swap(cachedPatterns[i],  cachedPatterns[j]);
    std::swap(cachedCPatterns[i], cachedCPatterns[j]);
    std::swap(safePatterns[i],    safePatterns[j]);
    std::swap(safeCPatterns[i],   safeCPatterns[j]);

    // #228: instead of leaving lastStepIndex at -1 (which causes the next processBlock
    // to fire the first hit retroactively), mark patternUpdated so the snapshot path
    // in processBlock absorbs the current step into lastStepIndex without firing.
    // Matches the semantics of updatePattern() — no spurious retroactive triggers.
    patternUpdated[i] = true;
    patternUpdated[j] = true;

    patternLock.store(false, std::memory_order_release);
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

    // #233: clamp negative beat positions to zero. Hosts can emit negative ppq
    // during pre-roll or REW-past-zero; without this, `globalStep` is negative,
    // `effectiveStep` stays negative even after the modulo (signed-int %), and
    // `pattern[stepIndex]` then indexes OOB → undefined behaviour.
    const auto globalStep    = std::max(0, static_cast<int>(beatPosition / StepLengthBeats));
    const int  effectiveStep = (masterLoopSteps > 0) ? (globalStep % masterLoopSteps) : globalStep;
    masterLoopCurrentStep.store(masterLoopSteps > 0 ? effectiveStep : 0, std::memory_order_relaxed);

    // Detect master loop wrap: effectiveStep decreased relative to the previous block.
    if (masterLoopSteps > 0 && lastEffectiveStep >= 0 && effectiveStep < lastEffectiveStep)
        result.masterLoopWrapped = true;
    lastEffectiveStep = effectiveStep;

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
                    // #234: assign() reuses existing storage when capacity is sufficient.
                    // safePatterns slots are reserve(256)'d in the ctor, so even a fresh
                    // 64-step pattern slotting in over an empty slot writes in place.
                    safePatterns[r].assign(cachedPatterns[r].begin(),  cachedPatterns[r].end());
                    safeCPatterns[r].assign(cachedCPatterns[r].begin(), cachedCPatterns[r].end());
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

        const int patLen      = static_cast<int>(pattern.size());
        const int stepIndex   = effectiveStep % patLen;
        const int prevStep    = lastStepIndex[r];

        if (stepIndex == prevStep) continue;   // no advance this block

        const auto& cPat   = safeCPatterns[r];
        const int   cLen   = static_cast<int>(cPat.size());

        // #232: walk every step crossed between prevStep (exclusive) and stepIndex
        // (inclusive), so a block that spans multiple steps doesn't drop hits.
        // firedMask stays one bit per rhythm — multiple hits inside a single block
        // are reported as one fire; accentMask reflects the last hit's accent state.
        if (prevStep < 0)
        {
            // First call: just check the current step. No back-walk possible.
            if (pattern[stepIndex])
            {
                result.firedMask |= (1 << r);
                if (cLen > 0 && cPat[stepIndex % cLen])
                    result.accentMask |= (1 << r);
            }
        }
        else
        {
            // Number of steps advanced this block. Modular forward distance,
            // capped at patLen so an extreme jump doesn't loop forever.
            int distance = (stepIndex - prevStep + patLen) % patLen;
            if (distance == 0) distance = patLen;   // full wrap
            distance = std::min(distance, patLen);

            for (int k = 1; k <= distance; ++k)
            {
                const int s = (prevStep + k) % patLen;

                // Rhythm-loop-wrap edge: we just stepped from patLen-1 onto 0.
                if (s == 0)
                    result.rhythmLoopWrapMask |= (1 << r);

                if (pattern[s])
                {
                    result.firedMask |= (1 << r);
                    if (cLen > 0 && cPat[s % cLen])
                        result.accentMask |= (1 << r);
                    else
                        result.accentMask &= ~(1 << r);
                }
            }
        }

        lastStepIndex[r] = stepIndex;
    }

    return result;
}
