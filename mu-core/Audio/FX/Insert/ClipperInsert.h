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
    void reset() override
    {
        // Snap the smoothers to their prepare() values so a reset slot ramps in exactly as a fresh one.
        smoothedThresh .setCurrentAndTargetValue(1.0f);
        smoothedOutGain.setCurrentAndTargetValue(1.0f);
    }

    void process(juce::AudioBuffer<float>& buf, int ns, int nCh,
                 const VoiceParams& p, float& /*grOut*/) override
    {
        // Slot 0 = Threshold 0..100 (encoded as 0..1 internal); Slot 1 = Output -24..0 dB.
        const float thresh   = juce::jlimit(0.001f, 1.0f, insertSlot(p, 0) / 100.0f);
        const float outputDb = insertSlot(p, 1);
        const float outGain  = std::pow(10.0f, outputDb / 20.0f);
        smoothedThresh .setTargetValue(thresh);
        smoothedOutGain.setTargetValue(outGain);
        // Sample-major: each smoothed value advances once per sample and every channel uses it, so the
        // right channel ramps with the left (channel-major looping left the right one stepping per block).
        auto** chans = buf.getArrayOfWritePointers();
        for (int i = 0; i < ns; ++i)
        {
            const float t  = smoothedThresh .getNextValue();
            const float og = smoothedOutGain.getNextValue();
            for (int ch = 0; ch < nCh; ++ch)
                chans[ch][i] = juce::jlimit(-t, t, chans[ch][i]) * og;
        }
    }

private:
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedThresh;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedOutGain;
};
