#pragma once

#include "FilterAlgorithmBase.h"
#include "Audio/AudioFilters.h"

// type 9 — 12 dB/oct all-pass via the project's `BiquadFilter`
// primitive. Doesn't change magnitude (within the filter's passband) but
// shifts phase around the cutoff — useful for phaser-style effects when
// chained with a wet/dry mix or used as part of a serial filter network.
class Ap12Filter : public FilterAlgorithmBase
{
public:
    void prepare(double sampleRate, int /*blockSize*/, int /*numChannels*/) override
    {
        currentSampleRate = sampleRate;
        reset();
    }
    void reset() override
    {
        eq[0].reset();
        eq[1].reset();
        lastCutoffHz  = -1.0f;
        lastResonance = -1.0f;
    }

    void process(juce::AudioBuffer<float>& buf, int numSamples, int numChannels,
                 float cutoffHz, float resonance) override
    {
        const bool dirty = (cutoffHz != lastCutoffHz) || (resonance != lastResonance);
        if (dirty)
        {
            const float q = 0.1f + resonance * 9.9f; // 0..0.99 → 0.1..9.9
            for (int ch = 0; ch < 2; ++ch)
                eq[ch].setAllPass(cutoffHz, q, (float) currentSampleRate);
            lastCutoffHz  = cutoffHz;
            lastResonance = resonance;
        }
        const int nCh = juce::jmin(numChannels, 2);
        for (int ch = 0; ch < nCh; ++ch)
        {
            auto* data = buf.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i)
                data[i] = eq[ch].process(data[i]);
        }
    }

private:
    double       currentSampleRate = 44100.0;
    BiquadFilter eq[2];
    float        lastCutoffHz  = -1.0f;
    float        lastResonance = -1.0f;
};
