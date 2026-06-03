#pragma once

#include "FilterAlgorithmBase.h"
#include <juce_dsp/juce_dsp.h>

// type 6 — 24 dB/oct bandpass via JUCE's LadderFilter (Moog-style).
class Bp24Filter : public FilterAlgorithmBase
{
public:
    void prepare(double sampleRate, int blockSize, int numChannels) override
    {
        const auto chans = static_cast<juce::uint32>(juce::jlimit(1, 2, numChannels));
        ladder.prepare({ sampleRate, static_cast<juce::uint32>(blockSize), chans });
        ladder.setDrive(1.0f);
        ladder.setMode(juce::dsp::LadderFilter<float>::Mode::BPF24);
    }
    void reset() override { ladder.reset(); lastCutoffHz = -1.0f; lastResonance = -1.0f; }

    void process(juce::AudioBuffer<float>& buf, int numSamples, int numChannels,
                 float cutoffHz, float resonance) override
    {
        // Skip the coefficient recompute when cutoff/resonance are unchanged.
        if (cutoffHz != lastCutoffHz || resonance != lastResonance)
        {
            ladder.setCutoffFrequencyHz(cutoffHz);
            ladder.setResonance(juce::jmax(0.01f, resonance));
            lastCutoffHz = cutoffHz; lastResonance = resonance;
        }
        juce::dsp::AudioBlock<float> block(buf.getArrayOfWritePointers(),
                                           static_cast<size_t>(numChannels), 0,
                                           static_cast<size_t>(numSamples));
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        ladder.process(ctx);
    }

private:
    juce::dsp::LadderFilter<float> ladder;
    float lastCutoffHz = -1.0f, lastResonance = -1.0f;
};
