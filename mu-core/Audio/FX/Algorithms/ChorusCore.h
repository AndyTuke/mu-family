#pragma once

#include "Audio/FX/Algorithms/ModDelayLine.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <cmath>

namespace mu_audio
{
// The chorus DSP shared by the send Effect slot and the Chorus insert: up to four delay taps swept by slightly detuned
// sine LFOs and summed. Real units in, wet signal out (no dry blend: each host blends for itself).
//   - Every voice reads one shared delay line per channel, so memory is one line per channel, not one per voice.
//   - Voice v starts v/4 of a turn into its LFO; the voices' rates are spread +-1.5 % so they never lock together.
//   - The right channel's swing on odd voices is widened by `spread`, which is what makes the output stereo.
// Allocation happens in prepare() only; tick() is allocation-free and lock-free.
class ChorusCore
{
public:
    static constexpr int MaxVoices = 4;

    struct Params
    {
        float rateHz   = 1.0f;    // LFO speed
        float depth    = 0.5f;    // 0..1, how much of swingMs is used
        float centreMs = 30.0f;   // the centre delay
        float swingMs  = 20.0f;   // the sweep either side of the centre at depth 1
        int   voices   = 2;       // 2..4
        float spread   = 0.5f;    // 0..1, extra stereo swing on the right channel's odd voices
    };

    // Sizes the delay lines for the worst read (centre + swing * 1.5 spread) at this rate.
    void prepare(double sampleRate)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        for (auto& l : line) l.prepare(sr, 0.07);
        for (auto* s : { &rate, &depth, &centre, &swing, &spread }) s->reset(sr, 0.015);
        reset();
    }

    // Silences the lines and returns the LFOs to their start phases; the next setParams() jumps straight to its values.
    void reset()
    {
        for (auto& l : line) l.clear();
        for (int v = 0; v < MaxVoices; ++v) phase[v] = (float) v / (float) MaxVoices;
        snap = true;
    }

    bool ready() const noexcept { return line[0].ready(); }

    // Sets the targets the smoothers glide to over 15 ms (the first call after reset() snaps).
    void setParams(const Params& p)
    {
        voices = juce::jlimit(2, MaxVoices, p.voices);
        set(rate, p.rateHz); set(depth, p.depth); set(centre, p.centreMs); set(swing, p.swingMs); set(spread, p.spread);
        snap = false;
    }

    // One stereo frame in, the wet pair out. A mono caller passes inR = inL and ignores wetR.
    void tick(float inL, float inR, float& wetL, float& wetR)
    {
        const float rateNow = rate.getNextValue(), depthNow = depth.getNextValue(), spreadNow = spread.getNextValue();
        const float centreS = centre.getNextValue() * 0.001f * (float) sr;
        const float swingS  = swing.getNextValue()  * 0.001f * (float) sr * depthNow;

        float sumL = 0.0f, sumR = 0.0f;
        for (int v = 0; v < voices; ++v)
        {
            const float lfo     = std::sin(phase[v] * juce::MathConstants<float>::twoPi);
            const float spreadV = (v % 2 == 0) ? 1.0f : (1.0f + spreadNow * 0.5f);
            // The swing never reaches zero delay (2 samples is the interpolator's floor).
            sumL += line[0].read(std::max(2.0f, centreS + lfo * swingS));
            sumR += line[1].read(std::max(2.0f, centreS + lfo * swingS * spreadV));

            // Per-voice LFO detune, evenly spread +-1.5 % so the voices average to the set rate.
            const float t      = (float) v / (float) (voices - 1);
            const float detune = 1.0f + (t - 0.5f) * 0.03f;
            phase[v] += rateNow * detune / (float) sr;
            if (phase[v] >= 1.0f) phase[v] -= 1.0f;
        }
        line[0].push(inL);
        line[1].push(inR);

        const float scale = 1.0f / (float) voices;
        wetL = sumL * scale;
        wetR = sumR * scale;
    }

private:
    using Smoother = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>;

    void set(Smoother& s, float v) { if (snap) s.setCurrentAndTargetValue(v); else s.setTargetValue(v); }

    double sr = 44100.0;
    ModDelayLine line[2];
    float phase[MaxVoices] = {};
    int   voices = 2;
    bool  snap = true;
    Smoother rate, depth, centre, swing, spread;
};
}  // namespace mu_audio
