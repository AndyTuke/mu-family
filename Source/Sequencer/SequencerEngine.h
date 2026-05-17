#pragma once

#include "Rhythm.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

struct BlockResult
{
    int  firedMask          = 0;  // bit N set = rhythm N fired a hit this block
    int  accentMask         = 0;  // bit N set = that hit was accented (Ring C coincidence)
    int  rhythmLoopWrapMask = 0;  // bit N set = rhythm N's step index wrapped to 0 this block
    bool masterLoopWrapped  = false; // master loop counter reset this block
};

class SequencerEngine : public juce::ChangeBroadcaster
{
public:
    static constexpr int    MaxRhythms      = 8;
    static constexpr double StepLengthBeats = 0.25; // 1/16th note default

    SequencerEngine();

    // Rhythm management — call from the message thread only.
    void    addRhythm    (const Rhythm& r);
    void    removeRhythm (int index);
    Rhythm& getRhythm    (int index);
    int     getNumRhythms() const { return (int)rhythms.size(); }

    // Call after changing any rhythm parameter to refresh its cached pattern.
    void updatePattern(int index);

    // #336 Stage B: audio-thread-safe pattern recompute using modulated euclid overrides.
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
    void setMasterLoopSteps(int steps)
    {
        masterLoopSteps = juce::jlimit(0, 256, steps);
        // #231: reset the wrap detector so a loop-length change mid-playback can't
        // emit a false `masterLoopWrapped` event from stale `lastEffectiveStep`
        // (false wraps gate hot-swap commits → premature staged swap).
        lastEffectiveStep = -1;
    }
    int  getMasterLoopSteps() const    { return masterLoopSteps; }

    // #231: call from PluginProcessor::processBlock on a transport stop→start edge
    // so the first block after restart doesn't see a false wrap from a stale
    // pre-stop `lastEffectiveStep`.
    void resetWrapDetector() { lastEffectiveStep = -1; }

    // #385: call from PluginProcessor::handleAsyncUpdate immediately after a hot-
    // swap commit (under suspendProcessing, so the audio thread is paused — no
    // synchronisation needed). Sentinel -1 tells processBlock's snapshot pass to
    // skip the absorb-current-step behaviour for this rhythm, so the new pattern
    // fires its first hit at the commit step instead of dropping it.
    void resetStepTrackingForSwap(int r)
    {
        if (r >= 0 && r < MaxRhythms)
        {
            lastStepIndex[r]       = -1;
            lastAccentStepIndex[r] = 0;
        }
    }

    // Current step within the master loop (0-based). 0 when loop is free-running.
    // Written by the audio thread each block; safe to read from the message thread at 10 Hz.
    int getMasterLoopCurrentStep() const { return masterLoopCurrentStep.load(std::memory_order_relaxed); }

    // Call from processBlock on the audio thread.
    // beatPosition is the current song position in beats (ppq).
    BlockResult processBlock(double beatPosition);

private:
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

    std::array<int, MaxRhythms> lastStepIndex;
    std::array<int, MaxRhythms> lastAccentStepIndex;
    std::vector<bool> patternUpdated;
    int masterLoopSteps = 0;
    std::atomic<int> masterLoopCurrentStep { 0 };
    int lastEffectiveStep = -1;  // previous block's effectiveStep, for master-loop wrap detection

    // #336 Stage B: audio-thread scratch buffers for tryUpdatePatternFromModulation.
    // Pre-reserved to 256 in ctor so the non-allocating pattern overloads never alloc.
    std::vector<bool> scratchPatA, scratchPatB, scratchEuclid, scratchEuclidC;
};
