#pragma once

#include "FilterAlgorithmBase.h"
#include <juce_dsp/juce_dsp.h>

// type 2 — 12 dB/oct bandpass via JUCE's StateVariableTPTFilter.
class Bp12Filter : public FilterAlgorithmBase
{
public:
    void prepare(double sampleRate, int blockSize, int numChannels) override
    {
        const auto chans = static_cast<juce::uint32>(juce::jlimit(1, 2, numChannels));
        svf.prepare({ sampleRate, static_cast<juce::uint32>(blockSize), chans });
        svf.setType(juce::dsp::StateVariableTPTFilterType::bandpass);
    }
    void reset() override { svf.reset(); }

    void process(juce::AudioBuffer<float>& buf, int numSamples, int numChannels,
                 float cutoffHz, float resonance) override
    {
        svf.setCutoffFrequency(cutoffHz);
        svf.setResonance(juce::jmax(0.01f, resonance));
        juce::dsp::AudioBlock<float> block(buf.getArrayOfWritePointers(),
                                           static_cast<size_t>(numChannels), 0,
                                           static_cast<size_t>(numSamples));
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        svf.process(ctx);
    }

private:
    juce::dsp::StateVariableTPTFilter<float> svf;
};
