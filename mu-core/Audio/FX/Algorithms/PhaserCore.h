#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace mu_audio
{
// The phaser DSP shared by the send Effect slot and the Phaser insert: a chain of first-order all-pass filters whose corner
// frequency is swept by a sine, with soft-limited feedback from the chain's output to its input. Real units in, wet signal
// out (no dry blend: the host adds the dry, which is what turns the all-passed signal into moving notches).
// All-pass coefficient: a = (tan(pi f / sr) - 1) / (tan(pi f / sr) + 1) (first-order section, Smith, "Physical Audio
// Signal Processing"). Allocation happens in prepare() only; tick() is allocation-free and lock-free.
class PhaserCore
{
public:
    static constexpr int MaxStages = 12;

    struct Params
    {
        float rateHz       = 0.5f;
        float depth        = 0.5f;    // 0..1, width of the sweep
        int   stages       = 6;       // rounded to an even count, 2..12
        float feedback     = 0.5f;    // 0..1, resonance of the notches
        float stereoOffset = 0.0f;    // turns of LFO the right channel runs behind the left
    };

    void prepare(double sampleRate)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        for (auto* s : { &rate, &depth, &feedback }) s->reset(sr, 0.015);
        reset();
    }

    // Clears the filter and feedback state and restarts the LFO; the next setParams() jumps straight to its values.
    void reset()
    {
        for (auto& z : state) z.fill(0.0f);
        fbState[0] = fbState[1] = 0.0f;
        phase = 0.0f;
        snap = true;
    }

    void setParams(const Params& p)
    {
        // Whole all-pass pairs, rounded to the nearest even count.
        numStages = juce::jlimit(2, MaxStages, 2 * juce::roundToInt((float) p.stages * 0.5f));
        offset = p.stereoOffset;
        set(rate, p.rateHz); set(depth, p.depth); set(feedback, p.feedback);
        snap = false;
    }

    // One stereo frame in, the wet pair out. A mono caller passes inR = inL and ignores wetR.
    void tick(float inL, float inR, float& wetL, float& wetR)
    {
        // The notch sweeps logarithmically from 200 Hz to 4 kHz as the LFO goes from -1 to +1 at full depth.
        static constexpr float fMin = 200.0f, fMax = 4000.0f;
        static constexpr float logRange = 2.9957323f;   // ln(fMax / fMin) = ln 20
        const float rateNow = rate.getNextValue(), depthNow = depth.getNextValue();
        const float fb = feedback.getNextValue() * 0.99f;
        const float maxHz = 0.45f * (float) sr;
        const float in[2] = { inL, inR };
        float out[2];
        for (int c = 0; c < 2; ++c)
        {
            const float lfo     = std::sin(juce::MathConstants<float>::twoPi * (phase + (c == 0 ? 0.0f : offset)));
            const float lfoNorm = 0.5f + 0.5f * lfo * depthNow;   // 0..1
            const float hz      = std::clamp(fMin * std::exp(lfoNorm * logRange), 20.0f, maxHz);
            const float t       = std::tan(juce::MathConstants<float>::pi * hz / (float) sr);
            const float a       = (t - 1.0f) / (t + 1.0f);

            // Soft-limit the fed-back signal so a near-unity feedback cannot build a huge resonance.
            float x = in[c] + std::tanh(fbState[c] * fb);
            for (int s = 0; s < numStages; ++s)
            {
                const float y = a * x + state[c][(size_t) s];   // first-order all-pass, transposed direct form II
                state[c][(size_t) s] = x - a * y;
                x = y;
            }
            fbState[c] = x;
            out[c] = x;
        }
        phase += rateNow / (float) sr;
        if (phase >= 1.0f) phase -= 1.0f;
        wetL = out[0];
        wetR = out[1];
    }

private:
    using Smoother = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>;

    void set(Smoother& s, float v) { if (snap) s.setCurrentAndTargetValue(v); else s.setTargetValue(v); }

    double sr = 44100.0;
    std::array<float, MaxStages> state[2] = {};
    float fbState[2] = {};
    float phase = 0.0f, offset = 0.0f;
    int   numStages = 6;
    bool  snap = true;
    Smoother rate, depth, feedback;
};
}  // namespace mu_audio
