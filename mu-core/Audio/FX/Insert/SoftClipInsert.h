#pragma once

#include "InsertAlgorithmBase.h"
#include <cmath>

// insertAlgo = 1. Soft clipping via anti-derivative anti-aliased (ADAA)
// tanh. The per-channel `xPrev` state remembers last input for the ADAA finite-
// difference; resetting it to 0 between voice retriggers is safe (the first
// sample of a new voice has no historical context anyway).
class SoftClipInsert : public InsertAlgorithmBase
{
public:
    void prepare(double sampleRate, int) override
    {
        // 15 ms ramps on preGain + outGain eliminate per-block step crackle on
        // drive/output knob movement.
        smoothedPreGain.reset(sampleRate, 0.015);  smoothedPreGain.setCurrentAndTargetValue(1.0f);
        smoothedOutGain.reset(sampleRate, 0.015);  smoothedOutGain.setCurrentAndTargetValue(1.0f);
        reset();
    }
    void reset() override { xPrev[0] = xPrev[1] = 0.0f; }

    void process(juce::AudioBuffer<float>& buf, int ns, int nCh,
                 const VoiceParams& p, float& /*grOut*/) override
    {
        // Slot 0 = Drive 0..100, Slot 1 = Output -24..0 dB. Slot 3 (LPF) is
        // applied downstream by InsertProcessor's post-drive tone filter.
        const float drive   = insertSlot(p, 0);
        const float outputDb = insertSlot(p, 1);
        const float preGain = std::pow(10.0f, drive / 100.0f * 2.0f);
        const float outGain = std::pow(10.0f, outputDb / 20.0f) / preGain;
        smoothedPreGain.setTargetValue(preGain);
        smoothedOutGain.setTargetValue(outGain);
        auto ad1Tanh = [](float x) -> float {
            return std::abs(x) > 12.0f ? std::abs(x) - 0.6931472f
                                       : std::log(std::cosh(x));
        };
        for (int ch = 0; ch < nCh; ++ch)
        {
            auto*  data = buf.getWritePointer(ch);
            float& xp   = xPrev[ch < 2 ? ch : 0];
            for (int i = 0; i < ns; ++i)
            {
                // ch0 advances the ramps; ch1 reads the latest values.
                const float pg = (ch == 0) ? smoothedPreGain.getNextValue()
                                           : smoothedPreGain.getCurrentValue();
                const float og = (ch == 0) ? smoothedOutGain.getNextValue()
                                           : smoothedOutGain.getCurrentValue();
                const float x  = data[i] * pg;
                const float dx = x - xp;
                float y = std::abs(dx) < 1e-4f ? std::tanh(0.5f * (x + xp))
                                               : (ad1Tanh(x) - ad1Tanh(xp)) / dx;
                data[i] = y * og;
                xp      = x;
            }
        }
    }

private:
    float xPrev[2] = {};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedPreGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedOutGain;
};
