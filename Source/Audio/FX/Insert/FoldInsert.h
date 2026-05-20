#pragma once

#include "InsertAlgorithmBase.h"
#include <cmath>

// insertAlgo = 3. Triangular foldback distortion — wraps the signal
// back inside [-1, +1] by reflecting whenever |x| exceeds 1. Stateless.
class FoldInsert : public InsertAlgorithmBase
{
public:
    void prepare(double sampleRate, int) override
    {
        smoothedPreGain.reset(sampleRate, 0.015);  smoothedPreGain.setCurrentAndTargetValue(1.0f);
        smoothedOutGain.reset(sampleRate, 0.015);  smoothedOutGain.setCurrentAndTargetValue(1.0f);
    }
    void reset() override {}

    void process(juce::AudioBuffer<float>& buf, int ns, int nCh,
                 const VoiceParams& p, float& /*grOut*/) override
    {
        const float preGain = std::pow(10.0f, p.insertDrive / 100.0f * 2.0f);
        const float outGain = std::pow(10.0f, p.insertOutput / 20.0f) / preGain;
        smoothedPreGain.setTargetValue(preGain);
        smoothedOutGain.setTargetValue(outGain);
        for (int ch = 0; ch < nCh; ++ch)
        {
            auto* data = buf.getWritePointer(ch);
            for (int i = 0; i < ns; ++i)
            {
                const float pg = (ch == 0) ? smoothedPreGain.getNextValue()
                                           : smoothedPreGain.getCurrentValue();
                const float og = (ch == 0) ? smoothedOutGain.getNextValue()
                                           : smoothedOutGain.getCurrentValue();
                float fx = juce::jlimit(-4.0f, 4.0f, data[i] * pg);
                while (fx > 1.0f || fx < -1.0f)
                {
                    if (fx >  1.0f) fx =  2.0f - fx;
                    if (fx < -1.0f) fx = -2.0f - fx;
                }
                data[i] = fx * og;
            }
        }
    }

private:
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedPreGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedOutGain;
};
