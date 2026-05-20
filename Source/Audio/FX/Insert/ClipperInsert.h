#pragma once

#include "InsertAlgorithmBase.h"
#include <cmath>

// insertAlgo = 5. Hard-clip at a user-set threshold + post-output gain.
// Distinct from HardClipInsert (which is a unity-threshold ADAA hard clipper);
// Clipper exposes the threshold as a knob so the user can sculpt sustain.
class ClipperInsert : public InsertAlgorithmBase
{
public:
    void prepare(double sampleRate, int) override
    {
        smoothedThresh .reset(sampleRate, 0.015);  smoothedThresh .setCurrentAndTargetValue(1.0f);
        smoothedOutGain.reset(sampleRate, 0.015);  smoothedOutGain.setCurrentAndTargetValue(1.0f);
    }
    void reset() override {}

    void process(juce::AudioBuffer<float>& buf, int ns, int nCh,
                 const VoiceParams& p, float& /*grOut*/) override
    {
        const float thresh  = juce::jlimit(0.001f, 1.0f, p.insertDrive / 100.0f);
        const float outGain = std::pow(10.0f, p.insertOutput / 20.0f);
        smoothedThresh .setTargetValue(thresh);
        smoothedOutGain.setTargetValue(outGain);
        for (int ch = 0; ch < nCh; ++ch)
        {
            auto* data = buf.getWritePointer(ch);
            for (int i = 0; i < ns; ++i)
            {
                const float t  = (ch == 0) ? smoothedThresh .getNextValue()
                                           : smoothedThresh .getCurrentValue();
                const float og = (ch == 0) ? smoothedOutGain.getNextValue()
                                           : smoothedOutGain.getCurrentValue();
                data[i] = juce::jlimit(-t, t, data[i]) * og;
            }
        }
    }

private:
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedThresh;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedOutGain;
};
