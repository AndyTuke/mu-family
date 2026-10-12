#include "Audio/SpinLock.h"   // mu-core: spin lock helpers
#include "Sequencer/RhythmSequencer.h"

#include <algorithm>
#include <cmath>

namespace mu_core {

RhythmSequencer::RhythmSequencer()
{
    lastStepIndex.fill(-1);
    lastAccentStepIndex.fill(0);
    wasLastStepHit.fill(false);
    stepSounded.fill(false);
    soundedFrom.fill(-1);
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
void RhythmSequencer::addRhythm(const Rhythm& r)
{
    if ((int)rhythms.size() >= MaxRhythms) return;
    rhythms.push_back(r);
    cachedPatterns.emplace_back();
    cachedCPatterns.emplace_back();
    patternUpdated.push_back(false);
    // safePatterns/safeCPatterns/lastStepIndex are fixed arrays — slot already exists.
    updatePattern((int)rhythms.size() - 1);
}

// Lock contract: the caller holds the processor's rhythmsLock with processing suspended. The audio-owned
// arrays below are written under patternLock only, which the audio thread merely try-locks, so without
// that outer exclusion they would race processBlock.
void RhythmSequencer::removeRhythm(int index)
{
    if (index < 0 || index >= (int)rhythms.size()) return;

    mu_core::spinLock(patternLock);

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
        stepSounded[i]         = stepSounded[i + 1];
        phaseOrigin[i]         = phaseOrigin[i + 1];
        pendingKind[i]         = pendingKind[i + 1];
        cachedOrigin[i]        = cachedOrigin[i + 1];
        cachedEpoch[i]         = cachedEpoch[i + 1];
        soundedFrom[i]         = soundedFrom[i + 1];
        boundaryEff[(size_t) i].store(boundaryEff[(size_t) i + 1].load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
    safePatterns[newN].clear();
    safeCPatterns[newN].clear();
    lastStepIndex[newN]       = -1;
    lastAccentStepIndex[newN] = 0;
    wasLastStepHit[newN]      = false;
    stepSounded[newN]         = false;
    phaseOrigin[newN]         = 0;
    pendingKind[newN]         = kNone;
    soundedFrom[newN]         = -1;
    boundaryEff[(size_t) newN].store(0, std::memory_order_relaxed);

    // A pending reorder holds its partner's slot index in cachedOrigin; slots after the removed one
    // moved down, so renumber it, and drop the request if the partner itself was removed.
    for (int i = 0; i < newN; ++i)
    {
        if (pendingKind[i] != kAdoptFrom) continue;
        if (cachedOrigin[i] == index)     pendingKind[i] = kNone;
        else if (cachedOrigin[i] > index) --cachedOrigin[i];
    }
    for (int i = 0; i < newN; ++i)
    {
        if (soundedFrom[i] == index)     soundedFrom[i] = -1;
        else if (soundedFrom[i] > index) --soundedFrom[i];
    }

    mu_core::spinUnlock(patternLock);
}

Rhythm& RhythmSequencer::getRhythm(int index)
{
    jassert(index >= 0 && index < (int)rhythms.size());
    return rhythms[index];
}

const Rhythm& RhythmSequencer::getRhythm(int index) const
{
    jassert(index >= 0 && index < (int)rhythms.size());
    return rhythms[index];
}

void RhythmSequencer::setNumRhythms(int n)
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
            stepSounded[i]         = false;
            phaseOrigin[i]         = 0;
            pendingKind[i]         = kNone;
            soundedFrom[i]         = -1;
            boundaryEff[(size_t) i].store(0, std::memory_order_relaxed);
        }
    }
}

// Lock contract as removeRhythm: callers hold rhythmsLock with processing suspended.
void RhythmSequencer::swapRhythmSlots(int i, int j)
{
    if (i == j) return;
    const int n = (int)rhythms.size();
    if (i < 0 || j < 0 || i >= n || j >= n) return;

    mu_core::spinLock(patternLock);

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

    // The patterns moved, so each one's phase goes with it. The audio thread owns phaseOrigin, so
    // it copies the partner's value itself on its next snapshot pass (cachedOrigin holds the partner
    // index for this kind); a hot-swap start still waiting travels with its pattern.
    std::swap(pendingKind[i],  pendingKind[j]);
    std::swap(cachedOrigin[i], cachedOrigin[j]);
    std::swap(cachedEpoch[i],  cachedEpoch[j]);
    if (pendingKind[i] == kNone) { pendingKind[i] = kAdoptFrom; cachedOrigin[i] = j; }
    if (pendingKind[j] == kNone) { pendingKind[j] = kAdoptFrom; cachedOrigin[j] = i; }
    std::swap(soundedFrom[i], soundedFrom[j]);
    if (soundedFrom[i] < 0) soundedFrom[i] = j;
    if (soundedFrom[j] < 0) soundedFrom[j] = i;

    mu_core::spinUnlock(patternLock);
}

void RhythmSequencer::rebuildCachedLocked(int index)
{
    // Demo cap: no generator is built with more steps than the cap allows.
    const int cap = stepCap.load(std::memory_order_relaxed);
    for (auto* gen : { &rhythms[index].genA, &rhythms[index].genB, &rhythms[index].genC })
        gen->steps = juce::jmin(gen->steps, cap);

    cachedPatterns[index]  = rhythms[index].getCombinedPattern();
    ensureMinPatternLength(cachedPatterns[index]);
    cachedCPatterns[index] = rhythms[index].genC.getPattern();
    patternUpdated[index]  = true;
}

void RhythmSequencer::updatePattern(int index)
{
    mu_core::spinLock(patternLock);
    rebuildCachedLocked(index);   // lastStepIndex preserved: processBlock absorbs the current step without firing
    mu_core::spinUnlock(patternLock);
}

void RhythmSequencer::requestSwapStart(int r, int originEff, uint32_t epoch)
{
    if (r < 0 || r >= (int) rhythms.size()) return;

    mu_core::spinLock(patternLock);
    rebuildCachedLocked(r);
    cachedOrigin[(size_t) r] = originEff;
    cachedEpoch [(size_t) r] = epoch;
    pendingKind [(size_t) r] = kStart;
    mu_core::spinUnlock(patternLock);
}

bool RhythmSequencer::tryUpdatePatternFromModulation(int index, const EuclidOverrides& ov)
{
    // Stage B: audio-thread pattern recompute. Writes directly into safePatterns/
    // safeCPatterns (already reserved to 256 in ctor) — bypasses cachedPatterns so a
    // concurrent message-thread updatePattern() that move-assigns into cached cannot
    // clobber the modulated pattern this block. The next snapshot pass (after a real
    // knob change sets patternUpdated[index]=true) will overwrite safePatterns with the
    // freshly-cached base, and the next modulation tick will re-modulate from there.
    if (index < 0 || index >= (int) rhythms.size()) return false;

    if (!mu_core::trySpinLock(patternLock))
        return false;   // message thread holds the lock — retry next block

    const Rhythm& rhy = rhythms[index];
    const int oldLen  = (int) safePatterns[index].size();
    const int oldCLen = (int) safeCPatterns[index].size();
    rhy.getCombinedPattern(ov, safePatterns[index], scratchPatA, scratchPatB, scratchEuclid);
    ensureMinPatternLength(safePatterns[index]);
    rhy.genC.getPattern(ov.c, safeCPatterns[index], scratchEuclidC);

    // Re-clamp lastStepIndex / lastAccentStepIndex against the new pattern lengths so
    // pattern-shrink modulation can't leave an out-of-range pointer that crashes the
    // next step-walk.
    const int newLen  = (int) safePatterns[index].size();
    const int newCLen = (int) safeCPatterns[index].size();
    // A length change keeps the pattern's phase: the previous block's position is re-derived under the
    // new length from the step counter, so the next walk advances one step rather than jumping.
    const int stepBefore = lastStepIndex[index];
    if (newLen > 0 && lastStepIndex[index] >= 0 && lastEffectiveStep >= 0 && newLen != oldLen)
        lastStepIndex[index] = floorMod(lastEffectiveStep - phaseOrigin[index], newLen);
    if (newCLen > 0 && lastAccentStepIndex[index] >= 0 && lastEffectiveStep >= 0 && newCLen != oldCLen)
        lastAccentStepIndex[index] = floorMod(lastEffectiveStep - phaseOrigin[index], newCLen);
    if (newLen  > 0 && lastStepIndex[index]       >= newLen)  lastStepIndex[index]       %= newLen;
    if (newCLen > 0 && lastAccentStepIndex[index] >= newCLen) lastAccentStepIndex[index] %= newCLen;

    // The position moved onto a step of the new pattern, so "did this step sound" describes that step.
    if (newLen > 0 && lastStepIndex[index] >= 0 && lastStepIndex[index] != stepBefore)
        wasLastStepHit[index] = safePatterns[index][(size_t) lastStepIndex[index]];

    mu_core::spinUnlock(patternLock);
    return true;
}

//==============================================================================
BlockResult RhythmSequencer::processBlock(double beatPosition)
{
    const int numRhythms = (int)rhythms.size();
    if (numRhythms == 0)
        return {};

    BlockResult result;

    // clamp negative beat positions to zero. Hosts can emit negative ppq
    // during pre-roll or REW-past-zero; without this, `globalStep` is negative,
    // `effectiveStep` stays negative even after the modulo (signed-int %), and
    // `pattern[stepIndex]` then indexes OOB → undefined behaviour.
    // The 1e-9 tolerance stops a beat that is a rounding hair below a step boundary (host ppq,
    // summed tempo maths) from flooring to the previous step and firing a block late.
    // A NaN or infinite host position would make the double-to-int cast undefined: hold the last good one.
    if (std::isfinite(beatPosition)) lastGoodBeat = beatPosition;
    else                             beatPosition = lastGoodBeat;
    const int globalStep     = beatToStep(beatPosition);
    // One read of the loop length for the whole block, so a concurrent change cannot split it.
    const int  loopSteps     = masterLoopSteps.load(std::memory_order_relaxed);
    const int  effectiveStep = (loopSteps > 0) ? (globalStep % loopSteps) : globalStep;
    masterLoopCurrentStep.store(loopSteps > 0 ? effectiveStep : 0, std::memory_order_relaxed);

    // Detect master loop wrap: effectiveStep decreased relative to the previous block.
    // No history after a reset request or a loop-length change: the step count restarts, not wraps.
    int prevEffectiveStep = lastEffectiveStep;
    const bool restartRequested = wrapResetRequested.exchange(false, std::memory_order_acq_rel);
    const bool historyDropped   = restartRequested || loopSteps != lastLoopSteps;
    if (historyDropped)
    {
        prevEffectiveStep = -1;
        // Without this a stopped load leaves the last step before the stop looking like a step
        // already played, so the commit rule would absorb the first step after play. A loop-length
        // change keeps playing, so the step it lands in may genuinely have been played already.
        if (restartRequested)
            prevGlobalStep = -1;
    }
    lastLoopSteps = loopSteps;
    if (loopSteps > 0 && prevEffectiveStep >= 0 && effectiveStep < prevEffectiveStep)
        result.masterLoopWrapped = true;

    // The step counter restarted (transport restart, loop change, master wrap, host jump back):
    // every pattern phase and recorded boundary belonged to the old run, so patterns realign to
    // the counter again and a swap flagged in the old run starts from 0.
    if (historyDropped || (lastEffectiveStep >= 0 && effectiveStep < lastEffectiveStep))
    {
        // A pattern whose length does not divide the loop (or that runs off-phase) would otherwise
        // walk up to a whole pass in the next block (an extra trigger, a false loop-wrap bit):
        // re-seed it to just before the realigned step, or to the cold-start sentinel on a transport
        // restart. A pattern still waiting on its first step (-1) keeps that sentinel.
        for (int r = 0; r < numRhythms; ++r)
        {
            const int len = (int) safePatterns[r].size();
            if (len <= 0) continue;
            if (restartRequested)
            {
                lastStepIndex[r]  = -1;
                wasLastStepHit[r] = false;
                stepSounded[r]    = false;
            }
            else if (lastStepIndex[r] != -1)
            {
                lastStepIndex[r]  = floorMod(effectiveStep - 1, len);
                // The tie flag describes the step the position now sits on, so a hit right after the
                // wrap is tied only if that step really was one (matters when the length does not divide the loop).
                wasLastStepHit[r] = safePatterns[r][(size_t) lastStepIndex[r]];
            }
        }
        phaseOrigin.fill(0);
        for (auto& b : boundaryEff) b.store(0, std::memory_order_relaxed);
        frameEpoch.store(frameEpoch.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    }
    lastEffectiveStep = effectiveStep;

    // Non-blocking snapshot: copy any updated patterns into the audio-thread-safe buffers.
    // If patternLock is held by the message thread, skip this block's snapshot — safePatterns
    // from the previous block will be used, avoiding any blocking or data race.
    {
        if (mu_core::trySpinLock(patternLock))
        {
            // Phases as they stood before this pass, for slots that adopt a partner's after a reorder.
            const auto adoptedOrigins = phaseOrigin;
            const auto adoptedSounded = stepSounded;
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

                    // After a reorder the pattern now here played in another slot: take that slot's "did the
                    // step sound" (audio-owned, so it is copied here rather than swapped with the slots). Done
                    // for every pending kind, so a swap start committed right after a reorder sees it too.
                    if (soundedFrom[r] >= 0)
                    {
                        stepSounded[r] = adoptedSounded[(size_t) soundedFrom[r]];
                        soundedFrom[r] = -1;
                    }

                    // A hot-swap start: the new pattern's step 0 sits on the swap's loop boundary.
                    // A commit landing in a step the old pattern already played absorbs that step
                    // (the new pattern starts on the next one, no double hit); a commit before the
                    // step was reached fires it at once. -1 is the "fire the current step" sentinel;
                    // the cold-start fill(-1) uses the same one.
                    const bool isSwapStart = (pendingKind[r] == kStart);
                    if (isSwapStart)
                    {
                        phaseOrigin[r] = (cachedEpoch[r] == kAnyFrameEpoch
                                          || cachedEpoch[r] == frameEpoch.load(std::memory_order_relaxed))
                                             ? cachedOrigin[r] : 0;
                        lastAccentStepIndex[r] = 0;
                        // Absorb the boundary step only when the old pattern actually sounded it: if it was
                        // a rest, absorbing would leave the new pattern without its downbeat, so it fires
                        // late instead (stepSounded still describes the step the old pattern is on).
                        // "Sounded" means the pattern bit: mute and solo act downstream in the mixer while the
                        // voice still triggers, so a muted step is inaudible either way and needs no special case.
                        const bool oldSoundedThisStep = prevGlobalStep >= 0 && globalStep == prevGlobalStep && stepSounded[r];
                        wasLastStepHit[r]      = false;   // the first new hit must not be "tied" to the old pattern
                        lastStepIndex[r]       = oldSoundedThisStep ? 0 : -1;
                    }
                    else if (pendingKind[r] == kAdoptFrom)
                    {
                        phaseOrigin[r] = adoptedOrigins[(size_t) cachedOrigin[r]];
                    }
                    pendingKind[r] = kNone;

                    // Absorb the current step so we don't retroactively fire a hit, except when
                    // lastStepIndex == -1 (cold start or a swap start ahead of its step): fire it.
                    // Mid-play knob changes leave lastStepIndex>=0 so they still absorb,
                    // preventing retroactive triggers. lastAccentStepIndex gates on the same sentinel.
                    if (!safePatterns[r].empty() && lastStepIndex[r] != -1)
                    {
                        lastStepIndex[r] = floorMod(effectiveStep - phaseOrigin[r], (int) safePatterns[r].size());
                        // The tie flag now describes the new pattern's bit for this step, but not for a swap
                        // start: that voice is fresh, so its first hit must not be tied (flag stays false).
                        // stepSounded is untouched: this step was not walked, so what sounded is unchanged.
                        if (! isSwapStart)
                            wasLastStepHit[r] = safePatterns[r][(size_t) lastStepIndex[r]];
                    }
                    if (!safeCPatterns[r].empty() && lastStepIndex[r] != -1)
                        lastAccentStepIndex[r] = floorMod(effectiveStep - phaseOrigin[r], (int) safeCPatterns[r].size());
                }
            }
            mu_core::spinUnlock(patternLock);
        }
    }

    for (int r = 0; r < numRhythms; ++r)
    {
        const auto& pattern = safePatterns[r];
        if (pattern.empty()) continue;

        const int patLen    = static_cast<int>(pattern.size());
        const int stepIndex = floorMod(effectiveStep - phaseOrigin[r], patLen);
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
            // and no temporal predecessor to be tied to, so tiedMask
            // stays clear regardless of pattern[stepIndex-1].
            if (stepIndex == 0)
                result.rhythmLoopWrapMask |= (1 << r);   // a pass starts here, so a swap staged now is flagged at it
            if (pattern[stepIndex])
            {
                result.firedMask |= (1 << r);
                if (cLen > 0 && cPat[stepIndex % cLen])
                    result.accentMask |= (1 << r);
            }
            wasLastStepHit[r] = pattern[stepIndex];
            stepSounded[r]    = pattern[stepIndex];
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
                stepSounded[r]    = isHit;
            }
        }

        // A pass of this pattern started in this block: remember where, for a swap flagged now.
        if (result.rhythmLoopWrapMask & (1 << r))
            boundaryEff[(size_t) r].store(effectiveStep - stepIndex, std::memory_order_relaxed);

        lastStepIndex[r]       = stepIndex;
        lastAccentStepIndex[r] = (cLen > 0) ? floorMod(effectiveStep - phaseOrigin[r], cLen) : 0;
    }

    prevGlobalStep = globalStep;

    return result;
}

} // namespace mu_core
