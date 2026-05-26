#include "SequencerEngine.h"

#include <algorithm>

SequencerEngine::SequencerEngine()
{
    lastStepIndex.fill(-1);
    lastAccentStepIndex.fill(0);
    wasLastStepHit.fill(false);
    rhythms.reserve(MaxRhythms);
    cachedPatterns.reserve(MaxRhythms);
    cachedCPatterns.reserve(MaxRhythms);
    patternUpdated.reserve(MaxRhythms);

    // pre-size every safePatterns / safeCPatterns slot to the worst-case
    // step count (256 — matches the resetSteps cap and getCombinedPattern's LCM
    // clamp). Audio-thread `assign()` from cachedPatterns later is then guaranteed
    // not to allocate, because the destination already has enough capacity.
    static constexpr size_t kMaxStepCount = 256;
    for (int i = 0; i < MaxRhythms; ++i)
    {
        safePatterns [i].reserve(kMaxStepCount);
        safeCPatterns[i].reserve(kMaxStepCount);
    }

    // Stage B: scratch buffers for audio-thread pattern recompute.
    scratchPatA   .reserve(kMaxStepCount);
    scratchPatB   .reserve(kMaxStepCount);
    scratchEuclid .reserve(kMaxStepCount);
    scratchEuclidC.reserve(kMaxStepCount);
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
        safePatterns[i]        = std::move(safePatterns[i + 1]);
        safeCPatterns[i]       = std::move(safeCPatterns[i + 1]);
        lastStepIndex[i]       = lastStepIndex[i + 1];
        lastAccentStepIndex[i] = lastAccentStepIndex[i + 1];
        wasLastStepHit[i]      = wasLastStepHit[i + 1];
    }
    safePatterns[newN].clear();
    safeCPatterns[newN].clear();
    lastStepIndex[newN]       = -1;
    lastAccentStepIndex[newN] = 0;
    wasLastStepHit[newN]      = false;

    patternLock.store(false, std::memory_order_release);
}

Rhythm& SequencerEngine::getRhythm(int index)
{
    jassert(index >= 0 && index < (int)rhythms.size());
    return rhythms[index];
}

const Rhythm& SequencerEngine::getRhythm(int index) const
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
            lastStepIndex[i]       = -1;
            lastAccentStepIndex[i] = 0;
            wasLastStepHit[i]      = false;
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

    // instead of leaving lastStepIndex at -1 (which fires the first new-pattern
    // hit retroactively), mark patternUpdated so the next processBlock runs through
    // the standard snapshot/absorb path which sets lastStepIndex[r] = effectiveStep
    // % size — matches updatePattern's semantics, no retroactive trigger.
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

bool SequencerEngine::tryUpdatePatternFromModulation(int index, const EuclidOverrides& ov)
{
    // Stage B: audio-thread pattern recompute. Writes directly into safePatterns/
    // safeCPatterns (already reserved to 256 in ctor) — bypasses cachedPatterns so a
    // concurrent message-thread updatePattern() that move-assigns into cached cannot
    // clobber the modulated pattern this block. The next snapshot pass (after a real
    // knob change sets patternUpdated[index]=true) will overwrite safePatterns with the
    // freshly-cached base, and the next modulation tick will re-modulate from there.
    if (index < 0 || index >= (int) rhythms.size()) return false;

    bool expected = false;
    if (!patternLock.compare_exchange_strong(expected, true, std::memory_order_acquire))
        return false;   // message thread holds the lock — retry next block

    const Rhythm& rhy = rhythms[index];
    rhy.getCombinedPattern(ov, safePatterns[index], scratchPatA, scratchPatB, scratchEuclid);
    rhy.genC.getPattern(ov.c, safeCPatterns[index], scratchEuclidC);

    // Re-clamp lastStepIndex / lastAccentStepIndex against the new pattern lengths so
    // pattern-shrink modulation can't leave an out-of-range pointer that crashes the
    // next step-walk.
    const int newLen  = (int) safePatterns[index].size();
    const int newCLen = (int) safeCPatterns[index].size();
    if (newLen  > 0 && lastStepIndex[index]       >= newLen)  lastStepIndex[index]       %= newLen;
    if (newCLen > 0 && lastAccentStepIndex[index] >= newCLen) lastAccentStepIndex[index] %= newCLen;

    patternLock.store(false, std::memory_order_release);
    return true;
}

//==============================================================================
BlockResult SequencerEngine::processBlock(double beatPosition)
{
    const int numRhythms = (int)rhythms.size();
    if (numRhythms == 0)
        return {};

    BlockResult result;

    // clamp negative beat positions to zero. Hosts can emit negative ppq
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
                    // assign() reuses existing storage when capacity is sufficient.
                    // safePatterns slots are reserve(256)'d in the ctor, so even a fresh
                    // pattern slotting in over an empty slot writes in place — no alloc
                    // on the audio thread.
                    safePatterns[r].assign(cachedPatterns[r].begin(),  cachedPatterns[r].end());
                    safeCPatterns[r].assign(cachedCPatterns[r].begin(), cachedCPatterns[r].end());
                    patternUpdated[r] = false;
                    // Absorb the current step so we don't retroactively fire a hit —
                    // EXCEPT when lastStepIndex == -1, which means "this is a step-
                    // aligned (re)start, fire the current step." That sentinel is
                    // set by:
                    //   (a) the ctor's fill(-1) — cold start, never-played rhythm
                    //       (#384 / #280: first hit lands in the first audio block
                    //       instead of being delayed by one step).
                    //   (b) PluginProcessor::handleAsyncUpdate's swap commit
                    //       (#385: hot-swapped rhythm fires its first hit at the
                    //       commit step instead of dropping it).
                    // Mid-play knob changes leave lastStepIndex>=0 so they still
                    // absorb, preventing retroactive triggers. lastAccentStepIndex
                    // gates on the same sentinel for the same reason.
                    if (!safePatterns[r].empty() && lastStepIndex[r] != -1)
                        lastStepIndex[r] = effectiveStep % (int)safePatterns[r].size();
                    if (!safeCPatterns[r].empty() && lastStepIndex[r] != -1)
                        lastAccentStepIndex[r] = effectiveStep % (int)safeCPatterns[r].size();
                }
            }
            patternLock.store(false, std::memory_order_release);
        }
    }

    for (int r = 0; r < numRhythms; ++r)
    {
        const auto& pattern = safePatterns[r];
        if (pattern.empty()) continue;

        const int patLen    = static_cast<int>(pattern.size());
        const int stepIndex = effectiveStep % patLen;
        const int prevStep  = lastStepIndex[r];

        if (stepIndex == prevStep) continue;   // no advance this block

        const auto& cPat = safeCPatterns[r];
        const int   cLen = static_cast<int>(cPat.size());

        // walk every step crossed between prevStep (exclusive) and stepIndex
        // (inclusive), so a block that spans multiple steps doesn't drop hits.
        // Common case (1 step advance) still runs the loop body once.
        // firedMask stays one bit per rhythm per block — multiple hits inside a
        // single block collapse to one fire; accentMask reflects the last hit.
        if (prevStep < 0)
        {
            // First call: just check the current step. No back-walk possible —
            // and no temporal predecessor to be tied to (#419), so tiedMask
            // stays clear regardless of pattern[stepIndex-1].
            if (pattern[stepIndex])
            {
                result.firedMask |= (1 << r);
                if (cLen > 0 && cPat[stepIndex % cLen])
                    result.accentMask |= (1 << r);
            }
            wasLastStepHit[r] = pattern[stepIndex];
        }
        else
        {
            // Modular forward distance, capped at patLen so an extreme jump
            // doesn't loop forever.
            int distance = (stepIndex - prevStep + patLen) % patLen;
            if (distance == 0) distance = patLen;   // full wrap
            distance = std::min(distance, patLen);

            for (int k = 1; k <= distance; ++k)
            {
                const int s = (prevStep + k) % patLen;

                if (s == 0)
                    result.rhythmLoopWrapMask |= (1 << r);

                const bool isHit = pattern[s];
                if (isHit)
                {
                    result.firedMask |= (1 << r);
                    if (cLen > 0 && cPat[s % cLen])
                        result.accentMask |= (1 << r);
                    else
                        result.accentMask &= ~(1 << r);

                    // tied = the step we just walked across, immediately
                    // before this hit, was also a hit. wasLastStepHit captures
                    // the truth of the previous step within this walk (or from
                    // a prior block's last walk). For multi-hit-in-one-block
                    // edge cases, the last hit in the walk is what determines
                    // the bit (matches firedMask's "one bit per rhythm per
                    // block" semantics — accentMask follows the same rule).
                    if (wasLastStepHit[r])
                        result.tiedMask |= (1 << r);
                    else
                        result.tiedMask &= ~(1 << r);
                }
                wasLastStepHit[r] = isHit;   // roll forward per-step
            }
        }

        lastStepIndex[r]       = stepIndex;
        lastAccentStepIndex[r] = (cLen > 0) ? (effectiveStep % cLen) : 0;
    }

    return result;
}
