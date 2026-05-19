#pragma once

#include "InsertAlgorithmBase.h"
#include <cmath>

// #425: insertAlgo = 9. Ring modulator — multiplies the input by a sinusoidal
// carrier at p.insertTone Hz. Per-channel phase accumulators preserve continuity
// across blocks. Mix knob (p.insertDrive) blends dry → ring-modulated.
class RingModInsert : public InsertAlgorithmBase
{
public:
    void prepare(double sampleRate, int) override
    {
        currentSampleRate = sampleRate;
        reset();
    }
    void reset() override { phase[0] = phase[1] = 0.0f; }

    void process(juce::AudioBuffer<float>& buf, int ns, int nCh,
                 const VoiceParams& p, float& /*grOut*/) override
    {
        const float mix   = p.insertDrive / 100.0f;
        const float freq  = juce::jlimit(10.0f, 5000.0f, p.insertTone);
        const float phInc = 2.0f * juce::MathConstants<float>::pi * freq
                            / (float)currentSampleRate;
        for (int ch = 0; ch < nCh; ++ch)
        {
            auto*  data = buf.getWritePointer(ch);
            float& ph   = phase[ch < 2 ? ch : 0];
            for (int i = 0; i < ns; ++i)
            {
                const float carrier = std::sin(ph);
                ph += phInc;
                if (ph >= juce::MathConstants<float>::twoPi)
                    ph -= juce::MathConstants<float>::twoPi;
                data[i] = data[i] * (1.0f - mix + carrier * mix);
            }
        }
    }

private:
    double currentSampleRate = 44100.0;
    float  phase[2] = {};
};
