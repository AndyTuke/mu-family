#pragma once

#include "InsertAlgorithmBase.h"
#include <cmath>

// insertAlgo = 5. Hard-clip at a user-set threshold + post-output gain.
// Distinct from HardClipInsert (which is a unity-threshold ADAA hard clipper);
// Clipper exposes the threshold as a knob so the user can sculpt sustain.
class ClipperInsert : public InsertAlgorithmBase
{
public:
    void prepare(double, int) override {}
    void reset()              override {}

    void process(juce::AudioBuffer<float>& buf, int ns, int nCh,
                 const VoiceParams& p, float& /*grOut*/) override
    {
        const float thresh  = juce::jlimit(0.001f, 1.0f, p.insertDrive / 100.0f);
        const float outGain = std::pow(10.0f, p.insertOutput / 20.0f);
        for (int ch = 0; ch < nCh; ++ch)
        {
            auto* data = buf.getWritePointer(ch);
            for (int i = 0; i < ns; ++i)
                data[i] = juce::jlimit(-thresh, thresh, data[i]) * outGain;
        }
    }
};
