#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>

// The Phrase layer's monophonic slice player (docs/mu-tilate/design.md): one slice sounds at a time, and the
// next slice cuts the previous one through a short automatic declick fade. Stage 1 plays a slice at its native
// speed or re-pitched (rate); the time-stretch drive (dry head, cross-fade into a constant-rate stretch) is the
// next stage and plugs in at the voice's read position.
namespace mu_tilate
{

// A slice of the loop: [start, end) in source samples.
struct SliceRange
{
    int start = 0;
    int end   = 0;
    int length() const noexcept { return end - start; }
};

// What one slice trigger asks for.
struct SliceParams
{
    float rate      = 1.0f;     // playback rate: 1 = native, 2 = an octave up (re-pitch)
    bool  reverse   = false;    // read the slice backwards
    float gain      = 1.0f;     // linear
    float fadeInMs  = 0.5f;     // every slice starts mid-waveform, so a short fade-in is never skipped
    float fadeOutMs = 0.5f;     // and ends with one
};

class SlicePlayer
{
public:
    // The cut fade: how long a slice that the next one interrupts takes to reach silence.
    static constexpr float kCutFadeMs = 3.0f;

    // Message thread, not processing: sets the rate the fades are timed in.
    void prepare(double sampleRate);
    // Audio thread: silences every voice at once (a stop or locate, where the caller has already faded the block).
    void reset() noexcept;

    // Audio thread: starts `range` of `src` now. A slice already sounding is cut through the cut fade. `src` is
    // read until the voice ends, so the layer keeps the loop alive until then (the hot-swap boundary).
    void trigger(const juce::AudioBuffer<float>* src, SliceRange range, const SliceParams& p) noexcept;
    // Audio thread: fades the slice that is sounding out through the cut fade (a gap in the timeline, a stop).
    void cut() noexcept;
    // Audio thread: adds the sounding voices into out[0, n); a mono source goes to every output channel.
    void render(juce::AudioBuffer<float>& out, int n) noexcept;

    bool isActive() const noexcept;

private:
    struct Voice
    {
        const juce::AudioBuffer<float>* src = nullptr;
        SliceRange range;
        double pos        = 0.0;   // read position in source samples (fractional)
        double step       = 1.0;   // signed samples per output sample
        float  gain       = 1.0f;
        int    age        = 0;     // output samples since the start
        int    fadeInLen  = 1;
        int    fadeOutLen = 1;
        bool   fading     = false; // being cut: cutGain falls to 0
        float  cutGain    = 1.0f;
        bool   active     = false;
    };

    // One output sample of the voice at its position (4-point Catmull-Rom, exact at integer positions), clamped to
    // the slice so a neighbouring slice never leaks in.
    static float read(const Voice& v, int channel) noexcept;
    // True while the voice's read position is inside its slice.
    static bool inRange(const Voice& v) noexcept;

    static constexpr int kVoices = 4;   // one playing plus the ones still fading out
    std::array<Voice, kVoices> voices {};
    double sr = 48000.0;
    float  cutStep = 1.0f / 144.0f;   // cutGain decrease per sample (1 / cut-fade samples)
};

} // namespace mu_tilate
