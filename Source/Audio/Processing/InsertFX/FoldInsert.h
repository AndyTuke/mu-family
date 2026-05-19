#pragma once

#include "InsertAlgorithmBase.h"
#include <cmath>

// #425: insertAlgo = 3. Triangular foldback distortion — wraps the signal
// back inside [-1, +1] by reflecting whenever |x| exceeds 1. Stateless.
class FoldInsert : public InsertAlgorithmBase
{
public:
    void prepare(double, int) override {}
    void reset()              override {}

    void process(juce::AudioBuffer<float>& buf, int ns, int nCh,
                 const VoiceParams& p, float& /*grOut*/) override
    {
        const float preGain = std::pow(10.0f, p.insertDrive / 100.0f * 2.0f);
        const float outGain = std::pow(10.0f, p.insertOutput / 20.0f) / preGain;
        for (int ch = 0; ch < nCh; ++ch)
        {
            auto* data = buf.getWritePointer(ch);
            for (int i = 0; i < ns; ++i)
            {
                float fx = juce::jlimit(-4.0f, 4.0f, data[i] * preGain);
                while (fx > 1.0f || fx < -1.0f)
                {
                    if (fx >  1.0f) fx =  2.0f - fx;
                    if (fx < -1.0f) fx = -2.0f - fx;
                }
                data[i] = fx * outGain;
            }
        }
    }
};
