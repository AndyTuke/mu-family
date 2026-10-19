#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

// A single polyphonic voice. Owns only a playback position.
// VoiceEngine owns the shared sample buffer and calls process() on each voice.
class SamplePlayer
{
public:
    // `fadeInSamples` — linear gain ramp applied over the first N samples
    // of playback. Default 0 = no ramp (transient-preserving for drum hits).
    // Pattern-legato tied triggers pass a small value (e.g. 64 samples ≈ 1.3 ms
    // @ 48 kHz) so the sample-voice restart doesn't click against a running
    // envelope tail. Atomic so the audio thread sees the value alongside the
    // triggered flag.
    void trigger(int fadeInSamples = 0);
    // per-sample playback ratios so the caller can deliver a sample-accurate
    // pitch envelope without restructuring the inner mix loop. With a constant ratio
    // (no envelope/mod), every entry in `ratios` is identical and the behaviour is
    // bit-equivalent to the old `double playbackRatio` overload.
    void process(const juce::AudioBuffer<float>& source,
                 const double*                    ratios,
                 juce::AudioBuffer<float>&        output,
                 int                             numSamples);
    // A block that keeps time without reading the sample (the engine's buffer lock was busy): a pending trigger
    // starts as in process(), then the position and the fade-in advance by `ratios` as a rendered block would. The
    // source's length is not known here, so a voice that passed its end stops at the next process().
    void skip(const double* ratios, int numSamples) noexcept;
    bool isActive() const;

private:
    double              playPos   = -1.0;   // -1 = inactive; audio thread only
    std::atomic<bool>   triggered { false };
    // fade-in countdown — when > 0, each rendered sample is multiplied by
    // a linear ramp from (1 - fadeInRemaining/fadeInTotal) to 1.0. Audio-thread
    // only after the initial trigger-time atomic exchange.
    std::atomic<int>    pendingFadeInSamples { 0 };
    int                 fadeInRemaining = 0;
    int                 fadeInTotal     = 0;
};
