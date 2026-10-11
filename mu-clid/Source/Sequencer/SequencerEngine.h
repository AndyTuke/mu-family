#pragma once

#include "Rhythm.h"
#include "MuLimits.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace mu_clid {

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

class SequencerEngine : public juce::ChangeBroadcaster
{
public:
    static constexpr int    MaxRhythms      = mu_limits::kMaxLayers;
    static constexpr double StepLengthBeats = 0.25; // 1/16th note default

    SequencerEngine();

    // Rhythm management — call from the message thread only.
    void    addRhythm    (const Rhythm& r);
    void    removeRhythm (int index);
    Rhythm& getRhythm    (int index);
    const Rhythm& getRhythm (int index) const;
    int     getNumRhythms() const { return (int)rhythms.size(); }

    // Call after changing any rhythm parameter to refresh its cached pattern.
    void updatePattern(int index);

    // Stage B: audio-thread-safe pattern recompute using modulated euclid overrides.
    // Non-allocating (all buffers pre-reserved in ctor). Uses try-lock on patternLock —
    // returns false if the message thread holds the lock; caller is expected to retry
    // on the next block (the override values are sticky in PluginProcessor).
    bool tryUpdatePatternFromModulation(int index, const EuclidOverrides& ov);

    // Resize all rhythm vectors to n — used during state loading to pre-expand or trim.
    void setNumRhythms(int n);

    // Swap two rhythm slots atomically with respect to the audio thread.
    void swapRhythmSlots(int i, int j);

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

    // True when the playhead moved back by more than a step between two blocks of a running
    // transport (a host loop jump or locate): to the wrap detector that looks like a master-loop
    // wrap, so the caller resets it. Smaller backward moves are tempo / nudge jitter, not a jump.
    static bool isBackwardBeatJump(double previousBeat, double beat) noexcept
    {
        return beat < previousBeat - StepLengthBeats;
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

    std::vector<Rhythm>            rhythms;

    // Message-thread authoritative copies. Written under patternLock.
    std::vector<std::vector<bool>> cachedPatterns;
    std::vector<std::vector<bool>> cachedCPatterns;

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
    // removeRhythm shift).
    std::array<bool, MaxRhythms> wasLastStepHit { };
    std::vector<bool> patternUpdated;

    // Hand-over from the message thread to the audio thread, guarded by patternLock with
    // patternUpdated. kStart = a hot-swap start (adopt the origin and apply the no-flam rule);
    // kAdoptFrom = a slot reorder (copy the partner slot's phase; cachedOrigin holds its index).
    enum PendingKind : uint8_t { kNone = 0, kStart, kAdoptFrom };
    std::array<uint8_t,  MaxRhythms> pendingKind  { };
    std::array<int,      MaxRhythms> cachedOrigin { };
    std::array<uint32_t, MaxRhythms> cachedEpoch  { };

    // Audio thread writes (relaxed): the effective step where each slot's pattern last wrapped to
    // its step 0, and the id of the step counter run (bumped whenever the counter restarts or wraps).
    std::array<std::atomic<int>, MaxRhythms> boundaryEff { };
    std::atomic<uint32_t> frameEpoch { 0 };

    void rebuildCachedLocked(int index);
    static int floorMod(int a, int n) noexcept { const int m = a % n; return m < 0 ? m + n : m; }
    // Atomic because host automation can set the loop length off the audio thread.
    std::atomic<int> masterLoopSteps { 0 };
    std::atomic<int> masterLoopCurrentStep { 0 };
    // Audio-thread private (never touched by another thread): the previous block's effective step and
    // loop length, for master-loop wrap detection. -1 = no history.
    int lastEffectiveStep = -1;
    int lastLoopSteps     = 0;
    std::atomic<bool> wrapResetRequested { false };

    // Stage B: audio-thread scratch buffers for tryUpdatePatternFromModulation.
    // Pre-reserved to 256 in ctor so the non-allocating pattern overloads never alloc.
    std::vector<bool> scratchPatA, scratchPatB, scratchEuclid, scratchEuclidC;
};

} // namespace mu_clid
