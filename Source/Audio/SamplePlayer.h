#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

// A single polyphonic voice. Owns only a playback position.
// VoiceEngine owns the shared sample buffer and calls process() on each voice.
class SamplePlayer
{
public:
    void trigger();
    // #220: per-sample playback ratios so the caller can deliver a sample-accurate
    // pitch envelope without restructuring the inner mix loop. With a constant ratio
    // (no envelope/mod), every entry in `ratios` is identical and the behaviour is
    // bit-equivalent to the old `double playbackRatio` overload.
    void process(const juce::AudioBuffer<float>& source,
                 const double*                    ratios,
                 juce::AudioBuffer<float>&        output,
                 int                             numSamples);
    bool isActive() const;

private:
    double              playPos   = -1.0;   // -1 = inactive; audio thread only
    std::atomic<bool>   triggered { false };
};
