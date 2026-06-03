#pragma once

#include "FilterAlgorithmBase.h"
#include <vector>
#include <cmath>

// type 15 — feedback comb with NEGATIVE feedback (Karplus-Strong feel).
//
//   y[n] = x[n] − g·y[n-D]   where D = sample_rate / cutoffHz
//
// The sign flip moves the resonant peaks to ODD multiples of f0/2 (f0/2,
// 3f0/2, 5f0/2…). Sounds darker / more "stringy" than the positive-feedback
// variant — the same delay-line, just inverted in the loop.
class CombMinusFilter : public FilterAlgorithmBase
{
public:
    void prepare(double sampleRate, int blockSize, int /*numChannels*/) override
    {
        currentSampleRate = sampleRate;
        const int maxCombSamples = static_cast<int>(sampleRate / 20.0) + 4;
        for (int ch = 0; ch < 2; ++ch)
        {
            buf[ch].assign(maxCombSamples, 0.0f);
            wPos[ch] = 0;
        }
        smoothedRes.reset(sampleRate, 0.005);  // 5 ms — eliminates resonance-knob crackle
        smoothedRes.setCurrentAndTargetValue(0.0f);
        // Delay length (= cutoff) is smoothed too: an abrupt cutoff change (e.g. a
        // stepped modulator) would jump the read tap and click. Ramping glides the
        // resonant pitch instead. 10 ms is short relative to a modulator step.
        smoothedDelay.reset(sampleRate, 0.010);
        smoothedDelay.setCurrentAndTargetValue(static_cast<float>(sampleRate / 1000.0));
        delayRamp.assign(static_cast<size_t>(juce::jmax(1, blockSize)), smoothedDelay.getCurrentValue());
    }
    void reset() override
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            std::fill(buf[ch].begin(), buf[ch].end(), 0.0f);
            wPos[ch] = 0;
        }
        smoothedDelay.setCurrentAndTargetValue(smoothedDelay.getTargetValue());  // no glide on reset
    }

    void process(juce::AudioBuffer<float>& audio, int numSamples, int numChannels,
                 float cutoffHz, float resonance) override
    {
        smoothedRes.setTargetValue(resonance);
        smoothedDelay.setTargetValue(static_cast<float>(currentSampleRate) / juce::jmax(20.0f, cutoffHz));
        // Pre-compute the per-sample delay ramp once so every channel reads the
        // identical glide (otherwise only ch0 advances it and ch1 would click).
        jassert(numSamples <= static_cast<int>(delayRamp.size()));
        for (int i = 0; i < numSamples; ++i)
            delayRamp[static_cast<size_t>(i)] = smoothedDelay.getNextValue();

        const int nCh = juce::jmin(numChannels, 2);
        for (int ch = 0; ch < nCh; ++ch)
        {
            auto&     b       = buf[ch];
            int&      w       = wPos[ch];
            const int bufSize = static_cast<int>(b.size());
            auto*     data    = audio.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i)
            {
                // ch0 advances the resonance ramp; ch1 reads the latest value.
                // Negate at use site — negative feedback is the defining property.
                const float g     = (ch == 0) ? -smoothedRes.getNextValue()
                                              : -smoothedRes.getCurrentValue();
                const float delayF = delayRamp[static_cast<size_t>(i)];
                const float readF = static_cast<float>(w) - delayF;
                const int   r0    = ((static_cast<int>(std::floor(readF)) % bufSize) + bufSize) % bufSize;
                const int   r1    = (r0 + 1) % bufSize;
                const float frac  = readF - std::floor(readF);
                const float delayed = b[r0] + frac * (b[r1] - b[r0]);
                const float out   = data[i] + g * delayed;
                b[w] = out;
                data[i] = out;
                w = (w + 1) % bufSize;
            }
        }
    }

private:
    double             currentSampleRate = 44100.0;
    std::vector<float> buf[2];
    int                wPos[2] = { 0, 0 };
    std::vector<float> delayRamp;   // per-sample smoothed delay length, shared across channels
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedRes;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedDelay;
};
