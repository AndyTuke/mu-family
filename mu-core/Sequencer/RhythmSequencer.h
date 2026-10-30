#pragma once

#include "Sequencer/Rhythm.h"
#include "MuLimits.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace mu_core {

struct BlockResult
{
    int  firedMask          = 0;  // bit N set = rhythm N fired a hit this block
    int  accentMask         = 0;  // bit N set = that hit was accented (Ring C coincidence)
    int  tiedMask           = 0;  // bit N set = rhythm N's hit this block is "tied"
                                  // (the immediately preceding step was also a hit). Audio
                                  // engine uses this when Rhythm::patternLegato is enabled
                                  // to skip the envelope noteOn so contiguous hits don't
                                  // restart the envelope. Always computed; the per-rhythm
                                  // patternLegato flag gates the actual behaviour at the
                                  // PluginProcessor trigger call.
    int  rhythmLoopWrapMask = 0;  // bit N set = rhythm N's step index wrapped to 0 this block
    bool masterLoopWrapped  = false; // master loop counter reset this block
};

class RhythmSequencer : public juce::ChangeBroadcaster
{
public:
    static constexpr int    MaxRhythms      = mu_limits::kMaxLayers;
    static constexpr double StepLengthBeats = 0.25; // 1/16th note default

    RhythmSequencer();

    // The sequencer does not own the rhythms: the product owns them and binds each live slot to its
    // Rhythm (null = unbound). Bind at construction or under the processor's layer lock, and rebind
    // whenever the product moves a Rhythm to another address.
    void    bindRhythm   (int slot, Rhythm* r);
    Rhythm& getRhythm    (int index);
    const Rhythm& getRhythm (int index) const;
    int     getNumRhythms() const { return numLive.load(std::memory_order_relaxed); }

    // Per-slot state moves — call from the message thread only. These move the sequencer's own
    // per-slot state (cached patterns, phase, step history, pending hand-overs); the Rhythm data
    // itself is moved by the product. They write audio-owned state under patternLock alone, so the
    // caller must hold the processor's layer lock (so processBlock is not running) and have
    // processing suspended, or those writes race the audio thread. removeSlotState does not change
    // the live count: the caller follows it with setNumRhythms(n - 1).
    void    removeSlotState (int index);
    void    swapSlotState   (int i, int j);

    // Call after changing any rhythm parameter to refresh its cached pattern.
    void updatePattern(int index);

    // Stage B: audio-thread-safe pattern recompute using modulated euclid overrides.
    // Non-allocating (all buffers pre-reserved in ctor). Uses try-lock on patternLock —
    // returns false if the message thread holds the lock; caller is expected to retry
    // on the next block (the override values are sticky in PluginProcessor).
    bool tryUpdatePatternFromModulation(int index, const EuclidOverrides& ov);

    // Sets the live slot count (state loading pre-expands or trims). The state of every slot that
    // becomes live or stops being live is cleared; nothing is created or destroyed.
    void setNumRhythms(int n);

    // UI read-only accessors (safe to call from message thread after processBlock).
    int getLastStepIndex       (int r) const { return lastStepIndex[r]; }
    int getLastAccentStepIndex (int r) const { return lastAccentStepIndex[r]; }
    int getPatternLength       (int r) const { return static_cast<int>(safePatterns[r].size()); }

    // Master loop length (0 = free-running, >0 = all rhythms reset to step 0 at this boundary).
    // Highest Euclid step count a pattern is built with (the demo cap; 64 when licensed).
    // updatePattern clamps each generator to it, so presets / automation can't exceed it.
    void setStepCap(int cap) noexcept { stepCap.store(juce::jlimit(1, 64, cap), std::memory_order_relaxed); }

    // Only stores the length: the audio thread notices the change in processBlock and drops its wrap
    // history itself, so a change between blocks cannot produce a false `masterLoopWrapped` (which
    // would commit a staged hot-swap early) and no other thread writes the audio-private history.
    void setMasterLoopSteps(int steps)
    {
        masterLoopSteps.store(juce::jlimit(0, 256, steps), std::memory_order_relaxed);
    }
    int  getMasterLoopSteps() const    { return masterLoopSteps.load(std::memory_order_relaxed); }

    // Asks the audio thread to forget its previous step before the next block, so the first block
    // after a transport restart cannot see a false wrap. A request flag, safe from any thread.
    void resetWrapDetector() { wrapResetRequested.store(true, std::memory_order_release); }

    // The step a beat position falls in (0 for negative or non-finite input). The 1e-9 tolerance stops a
    // beat that is a rounding hair below a step boundary from flooring to the previous step, and the
    // clamp keeps the double-to-int cast defined for any host value.
    static int beatToStep(double beat) noexcept
    {
        constexpr double kStepEpsilon = 1e-9;
        constexpr double kMaxStep     = 1073741824.0;   // 2^30 steps, far beyond any real song
        if (! std::isfinite(beat))
            return 0;
        const double step = std::floor(beat / StepLengthBeats + kStepEpsilon);
        return step <= 0.0 ? 0 : static_cast<int>(std::min(step, kMaxStep));
    }

    // True when the playhead moved back to an earlier step between two blocks of a running
    // transport (a host loop jump, a locate, or a nudge dipping across a step line): to the wrap
    // detector a lower step looks like a master-loop wrap, so the caller resets it. Steps are
    // compared, not raw beats, so a sub-step dip that crosses a step line counts too while one that
    // stays inside a step does not. A real jump (more than a step back) that lands exactly on a
    // master-loop boundary is left alone: that is a genuine wrap, which a staged swap needs to see.
    static bool isBackwardBeatJump(double previousBeat, double beat, int loopSteps = 0) noexcept
    {
        // A non-finite position has no step: report no jump rather than cast it.
        if (! std::isfinite(previousBeat) || ! std::isfinite(beat))
            return false;
        const int prevStep = beatToStep(previousBeat);
        const int landing  = beatToStep(beat);
        if (landing >= prevStep)
            return false;
        const bool realJump = beat < previousBeat - StepLengthBeats;
        return ! (realJump && loopSteps > 0 && landing % loopSteps == 0);
    }

    // What the transport code does with each block's start position (audio thread, allocation-free):
    //  - a non-finite host position carries on from the last good one;
    //  - a running transport that moved back by less than a step (a nudge, a sync-offset change, a tiny
    //    locate) is held where it was: resetting there would zero the swapped patterns' phase origins and
    //    could retrigger the step, while holding keeps one trigger per step;
    //  - a transport that just started, or a real backward jump, asks for a wrap-detector reset
    //    (isBackwardBeatJump leaves one that lands on a master-loop boundary alone: that is a real wrap).
    struct BlockBeat { double beat; bool resetWrap; };
    static BlockBeat resolveBlockBeat(double lastBeat, double beat, bool running, bool wasRunning, int loopSteps) noexcept
    {
        if (! std::isfinite(beat))
            beat = std::isfinite(lastBeat) ? lastBeat : 0.0;
        if (! running)
            return { beat, false };
        if (! wasRunning)
            return { beat, true };
        if (std::isfinite(lastBeat) && beat < lastBeat && beat >= lastBeat - StepLengthBeats)
            return { lastBeat, false };
        return { beat, isBackwardBeatJump(lastBeat, beat, loopSteps) };
    }

    // Message thread, after a hot-swap commit (and on loading a preset at once): rebuild slot r's
    // pattern and ask the audio thread to start it at its own step 0 on `originEff`, the effective
    // step where the swap's loop boundary fell, instead of the absolute step modulo its length.
    // `frameEpoch` is getFrameEpoch() when that boundary was recorded: if the step counter has
    // restarted since (master loop wrap, transport restart), the origin no longer applies and the
    // audio thread falls back to 0. The pattern rebuild and the request share one lock hold, so
    // the audio thread can never see the new pattern without its start.
    static constexpr uint32_t kAnyFrameEpoch = 0xffffffffu;
    void requestSwapStart(int r, int originEff, uint32_t frameEpoch = kAnyFrameEpoch);

    // Audio thread (HotSwapStager::checkBoundaries): the effective step where slot r's pattern
    // last started a pass, and the id of the current step counter run.
    int      getBoundaryEff(int r) const noexcept { return boundaryEff[(size_t) r].load(std::memory_order_relaxed); }
    uint32_t getFrameEpoch() const noexcept       { return frameEpoch.load(std::memory_order_relaxed); }

    // Current step within the master loop (0-based). 0 when loop is free-running.
    // Written by the audio thread each block; safe to read from the message thread at 10 Hz.
    int getMasterLoopCurrentStep() const { return masterLoopCurrentStep.load(std::memory_order_relaxed); }

    // Call from processBlock on the audio thread.
    // beatPosition is the current song position in beats (ppq).
    BlockResult processBlock(double beatPosition);

private:
    std::atomic<int> stepCap { 64 };

    // The live rhythm count and the product-owned Rhythm bound to each slot. The count is written
    // under the product's layer lock; the audio thread reads both only inside it.
    std::atomic<int>                 numLive { 0 };
    std::array<Rhythm*, MaxRhythms>  slotRhythm { };

    // Message-thread authoritative copies. Written under patternLock.
    std::array<std::vector<bool>, MaxRhythms> cachedPatterns;
    std::array<std::vector<bool>, MaxRhythms> cachedCPatterns;

    // Audio-thread read buffers. Fixed-size arrays — never reallocated, so the audio
    // thread can safely read them after releasing patternLock without OOB risk.
    // Snapshotted from cached* under patternLock at the start of each processBlock().
    std::array<std::vector<bool>, MaxRhythms> safePatterns;
    std::array<std::vector<bool>, MaxRhythms> safeCPatterns;

    // Guards cachedPatterns, cachedCPatterns, and patternUpdated.
    // Message thread: spin-wait to acquire. Audio thread: try-lock only (non-blocking).
    std::atomic<bool>            patternLock { false };

    // Audio-thread private (no other thread reads or writes these).
    std::array<int, MaxRhythms> lastStepIndex;
    std::array<int, MaxRhythms> lastAccentStepIndex;
    // Effective step at which each slot's pattern position is 0: position = (step - phaseOrigin)
    // modulo length. Moves only when a hot-swap starts a new pattern on its loop boundary.
    std::array<int, MaxRhythms> phaseOrigin { };
    int prevGlobalStep = -1;   // previous block's step, to tell a mid-step commit from a new step
    // per-rhythm "was the most recent step we walked a hit?" — used to
    // compute BlockResult::tiedMask on the next hit. Defaults to false; set
    // to false in the constructor's `fill(...)` initialisation block and
    // wherever lastStepIndex is reset to -1 (cold start, hot-swap commit,
    // removeSlotState shift).
    std::array<bool, MaxRhythms> wasLastStepHit { };
    // per-rhythm "did the step the position sits on sound?": set only by the step walk (a hit fired
    // = true, a rest = false), cleared on a transport restart. Absorbing a changed pattern or a
    // modulated length does not touch it, so the hot-swap commit rule sees what actually played.
    std::array<bool, MaxRhythms> stepSounded { };
    std::array<bool, MaxRhythms> patternUpdated { };

    // Hand-over from the message thread to the audio thread, guarded by patternLock with
    // patternUpdated. kStart = a hot-swap start (adopt the origin and apply the no-flam rule);
    // kAdoptFrom = a slot reorder (copy the partner slot's phase; cachedOrigin holds its index).
    enum PendingKind : uint8_t { kNone = 0, kStart, kAdoptFrom };
    std::array<uint8_t,  MaxRhythms> pendingKind  { };
    std::array<int,      MaxRhythms> cachedOrigin { };
    std::array<uint32_t, MaxRhythms> cachedEpoch  { };
    // After a reorder: the slot whose stepSounded this slot's pattern played in (-1 = none). Kept apart
    // from pendingKind so a swap start committed right after a reorder still carries it.
    std::array<int,      MaxRhythms> soundedFrom  { };

    // Audio thread writes (relaxed): the effective step where each slot's pattern last wrapped to
    // its step 0, and the id of the step counter run (bumped whenever the counter restarts or wraps).
    std::array<std::atomic<int>, MaxRhythms> boundaryEff { };
    std::atomic<uint32_t> frameEpoch { 0 };

    void rebuildCachedLocked(int index);
    // Returns slot i to its never-used state (patterns, phase, step history, pending hand-overs).
    void clearSlotState(int i);

    // The step walk advances only when the step index changes, so a one-step pattern would sound once
    // and never again. Tiling [x] to [x, x] keeps the same rhythm and gives the walk a step to advance.
    // Non-allocating on the audio path (the buffers are reserved to 256).
    static void ensureMinPatternLength(std::vector<bool>& pattern)
    {
        if (pattern.size() == 1)
            pattern.push_back(pattern.front());   // rt-ok: size 1 to 2; every pattern vector is reserved to 256 steps in the constructor, so push_back cannot reallocate
    }
    static int floorMod(int a, int n) noexcept { const int m = a % n; return m < 0 ? m + n : m; }
    // Atomic because host automation can set the loop length off the audio thread.
    std::atomic<int> masterLoopSteps { 0 };
    std::atomic<int> masterLoopCurrentStep { 0 };
    // Audio-thread private (never touched by another thread): the previous block's effective step and
    // loop length, for master-loop wrap detection. -1 = no history.
    int lastEffectiveStep = -1;
    double lastGoodBeat   = 0.0;   // last finite host position, used when a block arrives with NaN / inf
    int lastLoopSteps     = 0;
    std::atomic<bool> wrapResetRequested { false };

    // Stage B: audio-thread scratch buffers for tryUpdatePatternFromModulation.
    // Pre-reserved to 256 in ctor so the non-allocating pattern overloads never alloc.
    std::vector<bool> scratchPatA, scratchPatB, scratchEuclid, scratchEuclidC;
};

} // namespace mu_core
