#pragma once

#include "Audio/FX/Algorithms/ModDelayLine.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <cmath>

namespace mu_audio
{
// The flanger DSP shared by the send Effect slot and the Flanger insert: one short delay line per channel, its read point
// swept by a sine around 5.25 ms (so the swept delay crosses the undelayed dry signal and the comb passes through zero),
// with bipolar feedback. Real units in, wet signal out (no dry blend: each host blends for itself).
// Allocation happens in prepare() only; tick() is allocation-free and lock-free.
class FlangerCore
{
public:
    // The sweep is centred here; the wet delay therefore moves between 0 and twice this at full depth.
    static constexpr float kCentreMs = 5.25f;

    struct Params
    {
        float rateHz       = 0.5f;
        float depth        = 0.5f;    // 0..1, share of the centre delay the sweep covers
        float feedback     = 0.0f;    // -0.95..0.95, negative inverts the fed-back signal (hollower comb)
        float stereoOffset = 0.0f;    // turns of LFO the right channel runs behind the left
    };

    void prepare(double sampleRate)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        for (auto& l : line) l.prepare(sr, 0.012);   // the read reaches 10.5 ms at full depth
        for (auto* s : { &rate, &depth, &feedback }) s->reset(sr, 0.015);
        reset();
    }

    // Silences the lines and feedback and restarts the LFO; the next setParams() jumps straight to its values.
    void reset()
    {
        for (auto& l : line) l.clear();
        feed[0] = feed[1] = 0.0f;
        phase = 0.0f;
        snap = true;
    }

    bool ready() const noexcept { return line[0].ready(); }

    void setParams(const Params& p)
    {
        offset = p.stereoOffset;
        set(rate, p.rateHz); set(depth, p.depth);
        set(feedback, juce::jlimit(-0.95f, 0.95f, p.feedback));
        snap = false;
    }

    // One stereo frame in, the wet pair out. A mono caller passes inR = inL and ignores wetR.
    void tick(float inL, float inR, float& wetL, float& wetR)
    {
        const float rateNow = rate.getNextValue(), depthNow = depth.getNextValue(), fb = feedback.getNextValue();
        const float centreS = kCentreMs * 0.001f * (float) sr;
        const float in[2]   = { inL, inR };
        float wet[2];
        for (int c = 0; c < 2; ++c)
        {
            const float lfo = std::sin(juce::MathConstants<float>::twoPi * (phase + (c == 0 ? 0.0f : offset)));
            wet[c] = line[c].read(std::max(1.0f, centreS * (1.0f + depthNow * lfo)));
            // The tanh keeps even 95 % feedback from running away on loud input.
            line[c].push(in[c] + std::tanh(feed[c] * fb));
            feed[c] = wet[c];
        }
        phase += rateNow / (float) sr;
        if (phase >= 1.0f) phase -= 1.0f;
        wetL = wet[0];
        wetR = wet[1];
    }

private:
    using Smoother = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>;

    void set(Smoother& s, float v) { if (snap) s.setCurrentAndTargetValue(v); else s.setTargetValue(v); }

    double sr = 44100.0;
    ModDelayLine line[2];
    float feed[2] = {};
    float phase = 0.0f, offset = 0.0f;
    bool  snap = true;
    Smoother rate, depth, feedback;
};
}  // namespace mu_audio
