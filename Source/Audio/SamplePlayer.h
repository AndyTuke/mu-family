#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

// A single polyphonic voice. Owns only a playback position.
// VoiceEngine owns the shared sample buffer and calls process() on each voice.
class SamplePlayer
{
public:
    void trigger();
    void process(const juce::AudioBuffer<float>& source,
                 double                          playbackRatio,
                 juce::AudioBuffer<float>&        output,
                 int                             numSamples);
    bool isActive() const;

private:
    double              playPos   = -1.0;   // -1 = inactive; audio thread only
    std::atomic<bool>   triggered { false };
};
