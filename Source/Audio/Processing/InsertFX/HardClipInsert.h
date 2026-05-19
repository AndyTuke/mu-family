#pragma once

#include "InsertAlgorithmBase.h"
#include <cmath>

// #425: insertAlgo = 2. Hard clipping via anti-derivative anti-aliased (ADAA)
// at unity threshold (±1). Like SoftClipInsert but with a piecewise-quadratic
// anti-derivative instead of log(cosh). Distinct from ClipperInsert (case 5,
// user-set threshold, no ADAA).
class HardClipInsert : public InsertAlgorithmBase
{
public:
    void prepare(double, int) override { reset(); }
    void reset()              override { xPrev[0] = xPrev[1] = 0.0f; }

    void process(juce::AudioBuffer<float>& buf, int ns, int nCh,
                 const VoiceParams& p, float& /*grOut*/) override
    {
        const float preGain = std::pow(10.0f, p.insertDrive / 100.0f * 2.0f);
        const float outGain = std::pow(10.0f, p.insertOutput / 20.0f) / preGain;
        auto ad1Clip = [](float x) -> float {
            if (x >  1.0f) return  x - 0.5f;
            if (x < -1.0f) return -x - 0.5f;
            return x * x * 0.5f;
        };
        for (int ch = 0; ch < nCh; ++ch)
        {
            auto*  data = buf.getWritePointer(ch);
            float& xp   = xPrev[ch < 2 ? ch : 0];
            for (int i = 0; i < ns; ++i)
            {
                const float x  = data[i] * preGain;
                const float dx = x - xp;
                float y = std::abs(dx) < 1e-4f
                            ? juce::jlimit(-1.0f, 1.0f, 0.5f * (x + xp))
                            : (ad1Clip(x) - ad1Clip(xp)) / dx;
                data[i] = y * outGain;
                xp      = x;
            }
        }
    }

private:
    float xPrev[2] = {};
};
