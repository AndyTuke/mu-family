#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

// A single polyphonic voice. Owns only a playback position.
// VoiceEngine owns the shared sample buffer and calls process() on each voice.
class SamplePlayer
{
public:
    void trigger();
    // Sample-accurate variant (#220): per-sample playback ratios. Used by VoiceEngine
    // when pitch envelope and/or pitch modulation are present so the ratio updates
    // every sample instead of block-rate stair-stepping.
    void process(const juce::AudioBuffer<float>& source,
                 const double*                    ratios,
                 juce::AudioBuffer<float>&        output,
                 int                              numSamples);
    bool isActive() const;

private:
    double              playPos   = -1.0;   // -1 = inactive; audio thread only
    std::atomic<bool>   triggered { false };
};
