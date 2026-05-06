#pragma once

#include "Rhythm.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

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
    int     getNumRhythms() const { return numRhythms; }

    // Call after changing any rhythm parameter to refresh its cached pattern.
    void updatePattern(int index);

    // Direct numRhythms setter — used during state loading to pre-expand the active count.
    void setNumRhythms(int n) { numRhythms = juce::jlimit(0, MaxRhythms, n); }

    // UI read-only accessors (safe to call from message thread after processBlock).
    int getLastStepIndex(int r) const { return lastStepIndex[r]; }
    int getPatternLength (int r) const { return static_cast<int>(safePatterns[r].size()); }

    // Master loop length (0 = free-running, >0 = all rhythms reset to step 0 at this boundary).
    void setMasterLoopSteps(int steps) { masterLoopSteps = juce::jlimit(0, 256, steps); }
    int  getMasterLoopSteps() const    { return masterLoopSteps; }

    // Call from processBlock on the audio thread.
    // beatPosition is the current song position in beats (ppq).
    BlockResult processBlock(double beatPosition);

private:
    std::array<Rhythm,            MaxRhythms> rhythms;

    // Message-thread authoritative copies. Written under patternLock.
    std::array<std::vector<bool>, MaxRhythms> cachedPatterns;
    std::array<std::vector<bool>, MaxRhythms> cachedCPatterns;

    // Audio-thread read buffers. Snapshotted from cached* under patternLock at the
    // start of each processBlock(). Only the audio thread reads from these after snapshot.
    std::array<std::vector<bool>, MaxRhythms> safePatterns;
    std::array<std::vector<bool>, MaxRhythms> safeCPatterns;

    // Guards cachedPatterns, cachedCPatterns, and patternUpdated.
    // Message thread: spin-wait to acquire. Audio thread: try-lock only (non-blocking).
    std::atomic<bool>            patternLock { false };

    std::array<int,               MaxRhythms> lastStepIndex;
    std::array<bool,              MaxRhythms> patternUpdated{};
    int numRhythms      = 0;
    int masterLoopSteps = 0;
};
