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
    void reset() override
    {
        // Snap the gain smoothers to their prepare() values so a reset slot ramps in exactly as a fresh one.
        smoothedPreGain.setCurrentAndTargetValue(1.0f);
        smoothedOutGain.setCurrentAndTargetValue(1.0f);
    }

    void process(juce::AudioBuffer<float>& buf, int ns, int nCh,
                 const VoiceParams& p, float& /*grOut*/) override
    {
        const float drive    = insertSlot(p, 0);
        const float outputDb = insertSlot(p, 1);
        const float preGain  = std::pow(10.0f, drive / 100.0f * 2.0f);
        // Folding keeps the signal inside +/-1 at any drive, so the output gain is the Output knob alone
        // (dividing by preGain made loud drive settings nearly silent).
        const float outGain  = std::pow(10.0f, outputDb / 20.0f);
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
                // Triangle fold with period 4, closed form (no clamp, so a hard-driven input keeps folding):
                // t = (x + 1) mod 4 in [0, 4); rising 0..2 gives -1..+1, falling 2..4 gives +1..-1.
                const float x = data[i] * pg;
                float fx = 0.0f;
                if (std::isfinite(x))
                {
                    float t = std::fmod(x + 1.0f, 4.0f);
                    if (t < 0.0f) t += 4.0f;
                    fx = (t < 2.0f) ? t - 1.0f : 3.0f - t;
                }
                data[i] = fx * og;
            }
        }
    }

private:
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedPreGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedOutGain;
};
