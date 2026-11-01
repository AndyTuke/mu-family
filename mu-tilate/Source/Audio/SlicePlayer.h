#pragma once

#include "Sequencer/SliceSettings.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <memory>

// The Phrase layer's monophonic slice player (docs/mu-tilate/design.md): one slice sounds at a time, and the
// next slice cuts the previous one through a short automatic declick fade. A slice plays at its native speed,
// re-pitched (rate), or time-stretched to a new length (timeRatio): the stretched slice starts with a dry head,
// the source itself for the Transient hold, then cross-fades into a constant-rate Signalsmith Stretch
// (docs/mu-tilate/stretch-prototype-results.md, DECIDED 2026-10-31).
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

    // Time-stretch: output length / slice length (1.25 = 25 % longer at the same pitch). Used when it differs from 1,
    // rate is 1 and the slice is not reversed (those two play re-pitched or backwards, unstretched).
    float timeRatio = 1.0f;
    float holdMs    = 10.0f;    // Transient hold: how long the slice starts as the source itself
    float strength  = 1.0f;     // 0 = no dry head (a uniform stretch from the first sample), 1 = the full hold

    // The slice's own settings. The player uses pan, pitch, filter, both envelopes and loop; gain, fades and reverse are
    // the fields above (the caller has already combined them with the layer's and the block's).
    SliceSettings own;
};

class SlicePlayer
{
public:
    // The cut fade: how long a slice that the next one interrupts takes to reach silence.
    static constexpr float kCutFadeMs = 3.0f;
    // The cross-fade from the dry head into the stretch.
    static constexpr float kStretchFadeMs = 5.0f;
    // The stretch ratio is held to this range.
    static constexpr float kMinTimeRatio = 0.25f, kMaxTimeRatio = 4.0f;

    SlicePlayer();
    ~SlicePlayer();
    SlicePlayer(const SlicePlayer&) = delete;
    SlicePlayer& operator=(const SlicePlayer&) = delete;

    // Message thread, not processing: sets the rate the fades are timed in, and allocates the two stretchers and their
    // scratch for blocks of up to `maxBlock` samples (the one allocation point: render and trigger never allocate).
    // `shortWindow` selects the short analysis window (drums: better attacks, a quarter of the memory and latency);
    // the default window suits pads, vocals and tonal loops.
    void prepare(double sampleRate, int maxBlock = 512, bool shortWindow = false);
    // Audio thread: silences every voice at once (a stop or locate, where the caller has already faded the block).
    void reset() noexcept;

    // Audio thread: starts `range` of `src` now. A slice already sounding is cut through the cut fade. `src` is
    // read until the voice ends, so the layer keeps the loop alive until then (the hot-swap boundary).
    void trigger(const juce::AudioBuffer<float>* src, SliceRange range, const SliceParams& p) noexcept;
    // Audio thread: starts a pre-fade now: `range` of `src` (the first part of a slice) played backwards, swelling from silence to
    // p.gain over its whole length, so it ends at the slice's first sample. It is a voice of its own: it mixes over whatever
    // is sounding and a slice trigger or a cutSlices does not touch it. p.rate and p.own.pan / preTone shape it.
    void triggerPre(const juce::AudioBuffer<float>* src, SliceRange range, const SliceParams& p) noexcept;
    // Audio thread: fades the slice that is sounding out through the cut fade (a gap in the timeline). A pre-fade runs on.
    void cutSlices() noexcept;
    // Audio thread: fades everything out, the pre-fade too (a stop, a locate).
    void cut() noexcept;
    // Audio thread: adds the sounding voices into out[0, n); a mono source goes to every output channel.
    void render(juce::AudioBuffer<float>& out, int n) noexcept;

    bool isActive() const noexcept;

private:
    struct StretchState;   // the two Signalsmith Stretch instances and their scratch (SlicePlayer.cpp)

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

        // The slice's own shaping (SliceSettings): pan gains, loop, the pitch ratio, the filter and the two envelopes.
        float  panL = 1.0f, panR = 1.0f;
        bool   loop = false;
        bool   swell = false;          // the fade-in follows a squared ramp (the pre-fade)
        double pitch = 1.0;            // source samples per output sample from the pitch settings (1 = unchanged)
        SliceSettings own;
        float  cutStep    = 0.0f;      // cutGain decrease per sample once cut (the amp release, never faster than the cut fade)
        float  fltRel     = 0.0f;      // filter envelope release progress 0..1, runs once cut
        float  fltRelStep = 0.0f;
        float  ic1[2] = { 0.0f, 0.0f }, ic2[2] = { 0.0f, 0.0f };   // state-variable filter state, per channel
        float  fa1 = 0.0f, fa2 = 0.0f, fa3 = 0.0f, fk = 1.0f;      // its coefficients, refreshed every few samples

        // Stretch (stretcher < 0 means a native or re-pitched voice).
        int    stretcher  = -1;    // which of the two stretchers this voice owns
        double ratio      = 1.0;
        int    total      = 0;     // output length of the stretched slice
        int    holdSamples = 0;
        int    xfSamples  = 0;     // cross-fade length (0 when there is no dry head)
        bool   primed     = false; // the stretcher has been seeked
        int    startAge   = 0;     // output age at which the stretch becomes audible (the cross-fade starts)
        double inAcc      = 0.0;   // next source position to feed the stretcher, fractional (absolute source index)
        int    inPos      = 0;     // the same, as the count of source samples fed so far (absolute source index)
    };

    // One output sample of the voice at its position (4-point Catmull-Rom, exact at integer positions), clamped to
    // the slice so a neighbouring slice never leaks in.
    static float read(const Voice& v, int channel) noexcept;
    // True while the voice's read position is inside its slice.
    static bool inRange(const Voice& v) noexcept;

    // The shaping of one output frame (up to two channels): the filter, then the amp envelope, pan and gain into `out`.
    void emit(Voice& v, juce::AudioBuffer<float>& out, int index, float* frame, int channels, float gain) noexcept;
    // Refreshes the filter's coefficients from the cutoff and the filter envelope at the voice's age.
    void updateFilter(Voice& v) noexcept;
    static float envelope(double ageSamples, float aMs, float dMs, float sustain, double sampleRate) noexcept;

    void renderNative(Voice& v, juce::AudioBuffer<float>& out, int offset, int n) noexcept;
    void renderStretched(Voice& v, juce::AudioBuffer<float>& out, int offset, int n) noexcept;
    // Seeks the voice's stretcher so its output at age `startAge` is the source at startAge / ratio.
    void primeStretch(Voice& v, int startAge) noexcept;
    // Copies source samples [from, from + count) of channel `ch` (mono sources repeat channel 0) into dest, zero outside the slice.
    static void fetch(const Voice& v, int ch, int from, int count, float* dest) noexcept;

    static constexpr int kVoices = 4;   // one playing plus the ones still fading out
    std::array<Voice, kVoices> voices {};
    std::array<Voice, 2> preVoices {};   // the pre-fades: two, so one swelling into its hit is not cut off by the next (the older is replaced if both are busy)
    std::unique_ptr<StretchState> stretch;
    double sr = 48000.0;
    int    maxBlock = 512;
    float  cutStep = 1.0f / 144.0f;   // cutGain decrease per sample (1 / cut-fade samples)
    static constexpr int kFilterUpdate = 16;   // samples between filter coefficient updates
};

} // namespace mu_tilate
