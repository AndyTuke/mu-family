#pragma once

#include "Rhythm.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

struct BlockResult
{
    int firedMask  = 0;  // bit N set = rhythm N fired a hit this block
    int accentMask = 0;  // bit N set = that hit was accented (Ring C coincidence)
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

    // Resize all rhythm vectors to n — used during state loading to pre-expand or trim.
    void setNumRhythms(int n);

    // UI read-only accessors (safe to call from message thread after processBlock).
    int getLastStepIndex(int r) const { return lastStepIndex[r]; }
    int getPatternLength (int r) const { return static_cast<int>(safePatterns[r].size()); }

    // Master loop length (0 = free-running, >0 = all rhythms reset to step 0 at this boundary).
    void setMasterLoopSteps(int steps) { masterLoopSteps = juce::jlimit(0, 256, steps); }
    int  getMasterLoopSteps() const    { return masterLoopSteps; }

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
    std::vector<bool> patternUpdated;
    int masterLoopSteps = 0;
    std::atomic<int> masterLoopCurrentStep { 0 };
};
