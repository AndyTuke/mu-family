#pragma once

#include "InsertAlgorithmBase.h"
#include <cmath>

// #425: driveChar = 1. Soft clipping via anti-derivative anti-aliased (ADAA)
// tanh. The per-channel `xPrev` state remembers last input for the ADAA finite-
// difference; resetting it to 0 between voice retriggers is safe (the first
// sample of a new voice has no historical context anyway).
class SoftClipInsert : public InsertAlgorithmBase
{
public:
    void prepare(double, int) override { reset(); }
    void reset()              override { xPrev[0] = xPrev[1] = 0.0f; }

    void process(juce::AudioBuffer<float>& buf, int ns, int nCh,
                 const VoiceParams& p, float& /*grOut*/) override
    {
        const float preGain = std::pow(10.0f, p.driveDrive / 100.0f * 2.0f);
        const float outGain = std::pow(10.0f, p.driveOutput / 20.0f) / preGain;
        auto ad1Tanh = [](float x) -> float {
            return std::abs(x) > 12.0f ? std::abs(x) - 0.6931472f
                                       : std::log(std::cosh(x));
        };
        for (int ch = 0; ch < nCh; ++ch)
        {
            auto*  data  = buf.getWritePointer(ch);
            float& xp    = xPrev[ch < 2 ? ch : 0];
            for (int i = 0; i < ns; ++i)
            {
                const float x  = data[i] * preGain;
                const float dx = x - xp;
                float y = std::abs(dx) < 1e-4f ? std::tanh(0.5f * (x + xp))
                                               : (ad1Tanh(x) - ad1Tanh(xp)) / dx;
                data[i] = y * outGain;
                xp      = x;
            }
        }
    }

private:
    float xPrev[2] = {};
};
