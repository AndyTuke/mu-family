#pragma once

#include "FilterAlgorithmBase.h"
#include "Audio/AudioFilters.h"

// type 7 — 6 dB/oct lowpass via the project's `OnePoleLP` primitive
// (one per channel). Coefficient recompute is gated on cutoff/sample-rate
// changes — coefficient recompute skip is guarded by a cached last-value check.
class Lp6Filter : public FilterAlgorithmBase
{
public:
    void prepare(double sampleRate, int /*blockSize*/, int /*numChannels*/) override
    {
        currentSampleRate = sampleRate;
        reset();
    }
    void reset() override
    {
        lp[0].reset();
        lp[1].reset();
        lastCutoffHz = -1.0f;
    }

    void process(juce::AudioBuffer<float>& buf, int numSamples, int numChannels,
                 float cutoffHz, float /*resonance*/) override
    {
        const bool dirty = (cutoffHz != lastCutoffHz);
        if (dirty)
        {
            for (int ch = 0; ch < 2; ++ch)
                lp[ch].prepare(cutoffHz, (float) currentSampleRate);
            lastCutoffHz = cutoffHz;
        }
        const int nCh = juce::jmin(numChannels, 2);
        for (int ch = 0; ch < nCh; ++ch)
        {
            auto* data = buf.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i)
                data[i] = lp[ch].process(data[i]);
        }
    }

private:
    double    currentSampleRate = 44100.0;
    OnePoleLP lp[2];
    float     lastCutoffHz = -1.0f;
};
