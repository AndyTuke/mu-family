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

    // Call from processBlock on the audio thread.
    // beatPosition is the current song position in beats (ppq).
    // Returns a bitmask of rhythms that fired a hit this block (bit 0 = rhythm 0, etc.).
    int processBlock(double beatPosition);

private:
    std::array<Rhythm,            MaxRhythms> rhythms;
    std::array<std::vector<bool>, MaxRhythms> cachedPatterns;
    std::array<int,               MaxRhythms> lastStepIndex;
    int numRhythms = 0;
};
