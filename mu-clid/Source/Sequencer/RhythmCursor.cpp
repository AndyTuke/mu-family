// RhythmCursor / RhythmStepClock — mu_core::RhythmSequencer::processBlock split into the host's half
// and one rhythm's half. Every rule and comment below is the sequencer's, moved, so the two give the
// same steps; RhythmCursorEqualityTests drives both through the same beats.

#include "Sequencer/RhythmCursor.h"
#include "Audio/SpinLock.h"   // mu-core: spin lock helpers

#include <algorithm>
#include <cmath>

namespace mu_clid {

//==============================================================================
StepFrame RhythmStepClock::advance(double beatPosition) noexcept
{
    StepFrame f;

    // A NaN or infinite host position would make the double-to-int cast undefined: hold the last good one.
    if (std::isfinite(beatPosition)) lastGoodBeat = beatPosition;
    else                             beatPosition = lastGoodBeat;
    f.globalStep = RhythmSequencer::beatToStep(beatPosition);
    // One read of the loop length for the whole block, so a concurrent change cannot split it.
    const int loopSteps = masterLoopSteps.load(std::memory_order_relaxed);
    f.effectiveStep = (loopSteps > 0) ? (f.globalStep % loopSteps) : f.globalStep;
    masterLoopCurrentStep.store(loopSteps > 0 ? f.effectiveStep : 0, std::memory_order_relaxed);

    // Detect master loop wrap: effectiveStep decreased relative to the previous block.
    // No history after a reset request or a loop-length change: the step count restarts, not wraps.
    int prevEffectiveStep = lastEffectiveStep;
    f.restart = wrapResetRequested.exchange(false, std::memory_order_acq_rel);
    const bool historyDropped = f.restart || loopSteps != lastLoopSteps;
    if (historyDropped)
    {
        prevEffectiveStep = -1;
        // Without this a stopped load leaves the last step before the stop looking like a step
        // already played, so the commit rule would absorb the first step after play.
        if (f.restart)
            prevGlobalStep = -1;
    }
    lastLoopSteps = loopSteps;
    f.masterLoopWrapped = loopSteps > 0 && prevEffectiveStep >= 0 && f.effectiveStep < prevEffectiveStep;

    // The step counter restarted (transport restart, loop change, master wrap, host jump back): every
    // pattern realigns to the counter, and a swap flagged in the old run starts from 0.
    f.realign = historyDropped || (lastEffectiveStep >= 0 && f.effectiveStep < lastEffectiveStep);
    if (f.realign)
        frameEpoch.store(frameEpoch.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    lastEffectiveStep = f.effectiveStep;

    f.prevGlobalStep = prevGlobalStep;
    f.epoch          = frameEpoch.load(std::memory_order_relaxed);
    return f;
}

//==============================================================================
RhythmCursor::RhythmCursor()
{
    // Reserved to the worst-case step count (256, the resetSteps cap and getCombinedPattern's LCM clamp),
    // so the audio thread's assign() and the modulated recompute never allocate.
    static constexpr size_t kMaxStepCount = 256;
    for (auto* v : { &safePattern, &safeCPattern, &scratchPatA, &scratchPatB, &scratchEuclid, &scratchEuclidC })
        v->reserve(kMaxStepCount);
}

void RhythmCursor::rebuildCachedLocked(Rhythm& rhy, int stepCap)
{
    // Demo cap: no generator is built with more steps than the cap allows.
    for (auto* gen : { &rhy.genA, &rhy.genB, &rhy.genC })
        gen->steps = juce::jmin(gen->steps, stepCap);

    cachedPattern  = rhy.getCombinedPattern();
    ensureMinPatternLength(cachedPattern);
    cachedCPattern = rhy.genC.getPattern();
    patternUpdated = true;
}

void RhythmCursor::updatePattern(Rhythm& r, int stepCap)
{
    mu_core::spinLock(patternLock);
    rebuildCachedLocked(r, stepCap);   // lastStepIndex preserved: advance absorbs the current step without firing
    mu_core::spinUnlock(patternLock);
}

void RhythmCursor::requestSwapStart(Rhythm& r, int stepCap, int originEff, uint32_t epoch)
{
    mu_core::spinLock(patternLock);
    rebuildCachedLocked(r, stepCap);
    cachedOrigin = originEff;
    cachedEpoch  = epoch;
    pendingKind  = kStart;
    mu_core::spinUnlock(patternLock);
}

void RhythmCursor::requestAbsorb()
{
    // The cached (base) pattern moved with the rhythm, so the hand-over re-installs it, as the sequencer's
    // reorder does (a modulated pattern is re-applied by the next modulation pass).
    mu_core::spinLock(patternLock);
    patternUpdated = true;
    if (pendingKind != kStart) pendingKind = kAbsorb;   // a swap start still waiting keeps its start
    mu_core::spinUnlock(patternLock);
}

void RhythmCursor::clear()
{
    mu_core::spinLock(patternLock);
    cachedPattern.clear();
    cachedCPattern.clear();
    patternUpdated = false;
    pendingKind    = kNone;
    mu_core::spinUnlock(patternLock);
    safePattern.clear();
    safeCPattern.clear();
    lastStepIndex       = -1;
    lastAccentStepIndex = 0;
    wasLastStepHit      = false;
    stepSounded         = false;
    phaseOrigin         = 0;
    boundaryEff.store(0, std::memory_order_relaxed);
}

//==============================================================================
RhythmCursor::Hit RhythmCursor::advance(const StepFrame& f) noexcept
{
    Hit hit;
    const int effectiveStep = f.effectiveStep;

    // Realign: a pattern whose length does not divide the loop (or that runs off-phase) would otherwise
    // walk up to a whole pass in the next block (an extra trigger, a false loop-wrap bit): re-seed it to
    // just before the realigned step, or to the cold-start sentinel on a transport restart. A pattern
    // still waiting on its first step (-1) keeps that sentinel.
    if (f.realign)
    {
        const int len = (int) safePattern.size();
        if (len > 0)
        {
            if (f.restart)
            {
                lastStepIndex  = -1;
                wasLastStepHit = false;
                stepSounded    = false;
            }
            else if (lastStepIndex != -1)
            {
                lastStepIndex  = floorMod(effectiveStep - 1, len);
                // The tie flag describes the step the position now sits on, so a hit right after the
                // wrap is tied only if that step really was one.
                wasLastStepHit = safePattern[(size_t) lastStepIndex];
            }
        }
        phaseOrigin = 0;
        boundaryEff.store(0, std::memory_order_relaxed);
    }

    // Non-blocking snapshot of a hand-over from the message thread; a held lock leaves it for the next block.
    if (mu_core::trySpinLock(patternLock))
    {
        if (patternUpdated)
        {
            // assign() reuses the reserved storage: no allocation on the audio thread.
            safePattern.assign(cachedPattern.begin(),  cachedPattern.end());
            safeCPattern.assign(cachedCPattern.begin(), cachedCPattern.end());
            patternUpdated = false;

            // A hot-swap start: the new pattern's step 0 sits on the swap's loop boundary. A commit landing
            // in a step the old pattern already sounded absorbs that step (no double hit); otherwise the
            // step fires at once (-1 is the "fire the current step" sentinel, as at a cold start).
            const bool isSwapStart = (pendingKind == kStart);
            if (isSwapStart)
            {
                phaseOrigin = (cachedEpoch == kAnyFrameEpoch || cachedEpoch == f.epoch) ? cachedOrigin : 0;
                lastAccentStepIndex = 0;
                const bool oldSoundedThisStep = f.prevGlobalStep >= 0 && f.globalStep == f.prevGlobalStep && stepSounded;
                wasLastStepHit = false;   // the first new hit must not be "tied" to the old pattern
                lastStepIndex  = oldSoundedThisStep ? 0 : -1;
            }
            pendingKind = kNone;

            // Absorb the current step so a changed pattern does not fire it retroactively, except at the
            // -1 sentinel (cold start or a swap start ahead of its step): then it fires.
            if (! safePattern.empty() && lastStepIndex != -1)
            {
                lastStepIndex = floorMod(effectiveStep - phaseOrigin, (int) safePattern.size());
                // The tie flag now describes the new pattern's bit for this step, but not for a swap start.
                if (! isSwapStart)
                    wasLastStepHit = safePattern[(size_t) lastStepIndex];
            }
            if (! safeCPattern.empty() && lastStepIndex != -1)
                lastAccentStepIndex = floorMod(effectiveStep - phaseOrigin, (int) safeCPattern.size());
        }
        mu_core::spinUnlock(patternLock);
    }

    // The step walk.
    const auto& pattern = safePattern;
    if (pattern.empty())
        return hit;

    const int patLen    = static_cast<int>(pattern.size());
    const int stepIndex = floorMod(effectiveStep - phaseOrigin, patLen);
    const int prevStep  = lastStepIndex;

    if (stepIndex == prevStep)
        return hit;   // no advance this block

    const auto& cPat = safeCPattern;
    const int   cLen = static_cast<int>(cPat.size());

    // Walk every step crossed between prevStep (exclusive) and stepIndex (inclusive), so a block that spans
    // several steps drops no hit; several hits collapse to one fire, and the last hit decides accent and tie.
    if (prevStep < 0)
    {
        // First call: just this step, with no predecessor to be tied to.
        if (stepIndex == 0)
            hit.loopWrapped = true;   // a pass starts here, so a swap staged now is flagged at it
        if (pattern[(size_t) stepIndex])
        {
            hit.fired = true;
            if (cLen > 0 && cPat[(size_t) (stepIndex % cLen)])
                hit.accented = true;
        }
        wasLastStepHit = pattern[(size_t) stepIndex];
        stepSounded    = pattern[(size_t) stepIndex];
    }
    else
    {
        // Modular forward distance, capped at patLen so an extreme jump doesn't loop forever.
        int distance = (stepIndex - prevStep + patLen) % patLen;
        if (distance == 0) distance = patLen;   // full wrap
        distance = std::min(distance, patLen);

        for (int k = 1; k <= distance; ++k)
        {
            const int s = (prevStep + k) % patLen;

            if (s == 0)
                hit.loopWrapped = true;

            const bool isHit = pattern[(size_t) s];
            if (isHit)
            {
                hit.fired    = true;
                hit.accented = cLen > 0 && cPat[(size_t) (s % cLen)];
                // Tied = the step walked just before this hit was also a hit.
                hit.tied     = wasLastStepHit;
            }
            wasLastStepHit = isHit;   // roll forward per step
            stepSounded    = isHit;
        }
    }

    // A pass of this pattern started in this block: remember where, for a swap flagged now.
    if (hit.loopWrapped)
        boundaryEff.store(effectiveStep - stepIndex, std::memory_order_relaxed);

    lastStepIndex       = stepIndex;
    lastAccentStepIndex = (cLen > 0) ? floorMod(effectiveStep - phaseOrigin, cLen) : 0;
    return hit;
}

//==============================================================================
bool RhythmCursor::tryApplyModulatedPattern(const Rhythm& rhy, const EuclidOverrides& ov, int lastEffectiveStep) noexcept
{
    // Writes the audio-thread pattern directly, bypassing the cached copies, so a concurrent message-thread
    // updatePattern cannot clobber this block's modulated pattern; the next hand-over restores the base and
    // the next modulation tick re-modulates from there.
    if (! mu_core::trySpinLock(patternLock))
        return false;   // the message thread holds the lock: retry next block

    const int oldLen  = (int) safePattern.size();
    const int oldCLen = (int) safeCPattern.size();
    rhy.getCombinedPattern(ov, safePattern, scratchPatA, scratchPatB, scratchEuclid);
    ensureMinPatternLength(safePattern);
    rhy.genC.getPattern(ov.c, safeCPattern, scratchEuclidC);

    // Re-clamp the walked steps to the new lengths. A length change keeps the pattern's phase: the previous
    // block's position is re-derived under the new length from the step counter, so the next walk advances
    // one step rather than jumping.
    const int newLen  = (int) safePattern.size();
    const int newCLen = (int) safeCPattern.size();
    const int stepBefore = lastStepIndex;
    if (newLen > 0 && lastStepIndex >= 0 && lastEffectiveStep >= 0 && newLen != oldLen)
        lastStepIndex = floorMod(lastEffectiveStep - phaseOrigin, newLen);
    if (newCLen > 0 && lastAccentStepIndex >= 0 && lastEffectiveStep >= 0 && newCLen != oldCLen)
        lastAccentStepIndex = floorMod(lastEffectiveStep - phaseOrigin, newCLen);
    if (newLen  > 0 && lastStepIndex       >= newLen)  lastStepIndex       %= newLen;
    if (newCLen > 0 && lastAccentStepIndex >= newCLen) lastAccentStepIndex %= newCLen;

    // The position moved onto a step of the new pattern, so "did this step sound" describes that step.
    if (newLen > 0 && lastStepIndex >= 0 && lastStepIndex != stepBefore)
        wasLastStepHit = safePattern[(size_t) lastStepIndex];

    mu_core::spinUnlock(patternLock);
    return true;
}

} // namespace mu_clid
