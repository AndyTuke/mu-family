#pragma once

#include "FilterAlgorithmBase.h"
#include "Notch12Filter.h"

// type 10 — 24 dB/oct notch built from two cascaded Notch12 stages (each dry minus a unity-centre
// band-pass), so the centre nulls deeply at every resonance. The ladder band-pass cannot be used for
// this: its centre gain and phase vary with resonance, so dry minus ladder never cancels.
// Both stages use the same resonance-to-Q mapping as Notch12, so the notch narrows with resonance.
class Notch24Filter : public FilterAlgorithmBase
{
public:
    void prepare(double sampleRate, int blockSize, int numChannels) override
    {
        stageA.prepare(sampleRate, blockSize, numChannels);
        stageB.prepare(sampleRate, blockSize, numChannels);
    }
    void reset() override { stageA.reset(); stageB.reset(); }

    void process(juce::AudioBuffer<float>& buf, int numSamples, int numChannels,
                 float cutoffHz, float resonance) override
    {
        stageA.process(buf, numSamples, numChannels, cutoffHz, resonance);
        stageB.process(buf, numSamples, numChannels, cutoffHz, resonance);
    }

private:
    Notch12Filter stageA, stageB;
};
