#pragma once

#include "Rhythm.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

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

    // Master loop length (0 = free-running, >0 = all rhythms reset to step 0 at this boundary).
    void setMasterLoopSteps(int steps) { masterLoopSteps = juce::jlimit(0, 256, steps); }
    int  getMasterLoopSteps() const    { return masterLoopSteps; }

    // Call from processBlock on the audio thread.
    // beatPosition is the current song position in beats (ppq).
    // Returns a bitmask of rhythms that fired a hit this block (bit 0 = rhythm 0, etc.).
    int processBlock(double beatPosition);

private:
    std::array<Rhythm,            MaxRhythms> rhythms;
    std::array<std::vector<bool>, MaxRhythms> cachedPatterns;
    std::array<int,               MaxRhythms> lastStepIndex;
    std::array<bool,              MaxRhythms> patternUpdated{};
    int numRhythms      = 0;
    int masterLoopSteps = 0;
};
