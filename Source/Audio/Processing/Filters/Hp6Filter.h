#pragma once

#include "FilterAlgorithmBase.h"
#include "Audio/AudioFilters.h"

// #427: type 11 — 6 dB/oct highpass via the project's `OnePoleHP` primitive
// (one per channel). Cheapest filter on offer; useful as a DC blocker /
// rumble cut more than a tonal filter.
class Hp6Filter : public FilterAlgorithmBase
{
public:
    void prepare(double sampleRate, int /*blockSize*/, int /*numChannels*/) override
    {
        currentSampleRate = sampleRate;
        reset();
    }
    void reset() override
    {
        hp[0].reset();
        hp[1].reset();
        lastCutoffHz = -1.0f;
    }

    void process(juce::AudioBuffer<float>& buf, int numSamples, int numChannels,
                 float cutoffHz, float /*resonance*/) override
    {
        const bool dirty = (cutoffHz != lastCutoffHz);
        if (dirty)
        {
            for (int ch = 0; ch < 2; ++ch)
                hp[ch].prepare(cutoffHz, (float) currentSampleRate);
            lastCutoffHz = cutoffHz;
        }
        const int nCh = juce::jmin(numChannels, 2);
        for (int ch = 0; ch < nCh; ++ch)
        {
            auto* data = buf.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i)
                data[i] = hp[ch].process(data[i]);
        }
    }

private:
    double    currentSampleRate = 44100.0;
    OnePoleHP hp[2];
    float     lastCutoffHz = -1.0f;
};
