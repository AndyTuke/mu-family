#pragma once

#include "InsertAlgorithmBase.h"
#include "Audio/AudioFilters.h"
#include <cmath>

// #425: driveChar = 10. Tape saturation — preGain → tanh → DC block → tone
// LP → output trim. The DC block sits between the non-linearity (which can
// generate DC offset for asymmetric input) and the tone filter, so the LP
// doesn't have to handle a slow drift.
class TapeSatInsert : public InsertAlgorithmBase
{
public:
    void prepare(double sampleRate, int) override
    {
        currentSampleRate = sampleRate;
        reset();
    }
    void reset() override
    {
        toneFilter[0].reset();
        toneFilter[1].reset();
        dcIn [0] = dcIn [1] = 0.0f;
        dcOut[0] = dcOut[1] = 0.0f;
    }

    void process(juce::AudioBuffer<float>& buf, int ns, int nCh,
                 const VoiceParams& p, float& /*grOut*/) override
    {
        const float preGain = 1.0f + (p.driveDrive / 100.0f) * 9.0f;   // 1..10×
        const float outGain = std::pow(10.0f, p.driveOutput / 20.0f);
        const float toneHz  = juce::jlimit(200.0f, 20000.0f, p.driveTone);
        const float dcCoeff = 1.0f - (2.0f * juce::MathConstants<float>::pi * 20.0f
                                      / (float)currentSampleRate);
        for (int ch = 0; ch < nCh; ++ch)
            toneFilter[ch].prepare(toneHz, (float)currentSampleRate);
        for (int ch = 0; ch < nCh; ++ch)
        {
            auto*  data = buf.getWritePointer(ch);
            float& di   = dcIn [ch < 2 ? ch : 0];
            float& dout = dcOut[ch < 2 ? ch : 0];
            for (int i = 0; i < ns; ++i)
            {
                const float sat = std::tanh(data[i] * preGain);
                const float dc  = sat - di + dcCoeff * dout;
                di   = sat;
                dout = dc;
                data[i] = toneFilter[ch].process(dc) * outGain;
            }
        }
    }

private:
    double    currentSampleRate = 44100.0;
    OnePoleLP toneFilter[2];
    float     dcIn [2] = {};
    float     dcOut[2] = {};
};
