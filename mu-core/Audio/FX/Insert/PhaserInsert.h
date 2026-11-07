#pragma once

#include "InsertAlgorithmBase.h"
#include <cmath>

// insertAlgo = 19. Phaser: six first-order all-pass filters in a row whose corner frequency is swept by a slow sine; mixed
// with the dry input they make moving notches in the spectrum. Some of the last stage's output is fed back to the input.
//   - Rate      LFO speed, 0.05..5 Hz.
//   - Depth     how wide the sweep is (0 holds it at the middle of the range, 200 Hz..4 kHz on a log scale).
//   - Feedback  0..90 %: resonance of the notches.
//   - Mix       dry/wet crossfade.
// All-pass coefficient: a = (tan(pi f / sr) - 1) / (tan(pi f / sr) + 1) (first-order section, Smith, "Physical Audio
// Signal Processing"). The right channel's LFO is a quarter turn behind the left.
class PhaserInsert : public InsertAlgorithmBase
{
public:
    void prepare(double sampleRate, int) override
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        mix.reset(sr, 0.02);
        fb.reset(sr, 0.02);
        reset();
    }

    void reset() override
    {
        for (int c = 0; c < 2; ++c) { for (auto& z : state[c]) z = 0.0f; last[c] = 0.0f; }
        phase = 0.0f;
        mix.setCurrentAndTargetValue(0.0f);
        fb.setCurrentAndTargetValue(0.0f);
        started = false;
    }

    void process(juce::AudioBuffer<float>& buf, int ns, int nCh,
                 const VoiceParams& p, float& /*grOut*/) override
    {
        const int nChState = juce::jmin(nCh, 2);
        // Slot 0 = Rate 0.05..5 Hz (log), 1 = Depth 0..100 %, 2 = Feedback 0..90 %, 3 = Mix 0..100 %.
        const float rate   = juce::jlimit(0.05f, 5.0f, insertSlot(p, 0));
        const float depth  = juce::jlimit(0.0f, 1.0f, insertSlot(p, 1) * 0.01f);
        const float fbAmt  = juce::jlimit(0.0f, 0.9f, insertSlot(p, 2) * 0.01f);
        const float mixAmt = juce::jlimit(0.0f, 1.0f, insertSlot(p, 3) * 0.01f);
        if (! started) { mix.setCurrentAndTargetValue(mixAmt); fb.setCurrentAndTargetValue(fbAmt); started = true; }
        mix.setTargetValue(mixAmt);
        fb.setTargetValue(fbAmt);

        const float inc    = rate / (float) sr;
        const float lo     = 200.0f, hi = 4000.0f;
        const float centre = std::sqrt(lo * hi);                     // geometric middle of the range
        const float octSpan = 0.5f * std::log2(hi / lo) * depth;     // octaves either side of the centre at full depth
        const float maxHz  = 0.45f * (float) sr;
        auto chans = buf.getArrayOfWritePointers();
        for (int i = 0; i < ns; ++i)
        {
            const float m = mix.getNextValue();
            const float f = fb.getNextValue();
            for (int ch = 0; ch < nChState; ++ch)
            {
                const float ph  = phase + (ch == 0 ? 0.0f : 0.25f);
                const float lfo = std::sin(juce::MathConstants<float>::twoPi * ph);
                const float hz  = juce::jlimit(20.0f, maxHz, centre * std::exp2(octSpan * lfo));
                const float t   = std::tan(juce::MathConstants<float>::pi * hz / (float) sr);
                const float a   = (t - 1.0f) / (t + 1.0f);

                const float in = chans[ch][i];
                float x = in + last[ch] * f;
                x = std::tanh(x);   // keeps the resonance bounded
                for (auto& z : state[ch])
                {
                    const float y = a * x + z;     // first-order all-pass, transposed direct form II
                    z = x - a * y;
                    x = y;
                }
                last[ch] = x;
                chans[ch][i] = in * (1.0f - m) + (in + x) * 0.5f * m;
            }
            phase += inc;
            if (phase >= 1.0f) phase -= 1.0f;
        }
    }

private:
    static constexpr int kStages = 6;
    double sr = 44100.0;
    float state[2][kStages] = {};
    float last[2] = {};
    float phase = 0.0f;
    bool  started = false;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mix, fb;
};
