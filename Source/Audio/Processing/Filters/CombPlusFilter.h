#pragma once

#include "FilterAlgorithmBase.h"
#include <vector>
#include <cmath>

// #427: type 8 — feedback comb with POSITIVE feedback.
//
//   y[n] = x[n] + g·y[n-D]   where D = sample_rate / cutoffHz
//
// Resonances pile up at f0, 2f0, 3f0… (the integer harmonics of f0). Good for
// tuned drones, plucked-string textures, and metallic resonator effects.
// The delay buffer is sized for the lowest pitch (20 Hz cutoff floor) and the
// read tap uses linear interpolation between adjacent samples so the resonant
// frequency is continuous-valued, not quantised to integer delay lengths.
class CombPlusFilter : public FilterAlgorithmBase
{
public:
    void prepare(double sampleRate, int /*blockSize*/, int /*numChannels*/) override
    {
        currentSampleRate = sampleRate;
        const int maxCombSamples = static_cast<int>(sampleRate / 20.0) + 4;
        for (int ch = 0; ch < 2; ++ch)
        {
            buf[ch].assign(maxCombSamples, 0.0f);
            wPos[ch] = 0;
        }
    }
    void reset() override
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            std::fill(buf[ch].begin(), buf[ch].end(), 0.0f);
            wPos[ch] = 0;
        }
    }

    void process(juce::AudioBuffer<float>& audio, int numSamples, int numChannels,
                 float cutoffHz, float resonance) override
    {
        const float delayF = static_cast<float>(currentSampleRate) / juce::jmax(20.0f, cutoffHz);
        const float g      = resonance;   // positive feedback
        const int   nCh    = juce::jmin(numChannels, 2);
        for (int ch = 0; ch < nCh; ++ch)
        {
            auto&     b       = buf[ch];
            int&      w       = wPos[ch];
            const int bufSize = static_cast<int>(b.size());
            auto*     data    = audio.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i)
            {
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
};
