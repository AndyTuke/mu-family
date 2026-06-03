#pragma once

#include "FilterAlgorithmBase.h"
#include <juce_dsp/juce_dsp.h>

// type 3 — 12 dB/oct notch synthesised as dry minus bandpass via the
// JUCE StateVariableTPTFilter. The dry signal is staged in a scratch buffer
// before the SVF mutates it in place, then subtracted on the way out.
class Notch12Filter : public FilterAlgorithmBase
{
public:
    void prepare(double sampleRate, int blockSize, int numChannels) override
    {
        const auto chans = static_cast<juce::uint32>(juce::jlimit(1, 2, numChannels));
        svf.prepare({ sampleRate, static_cast<juce::uint32>(blockSize), chans });
        svf.setType(juce::dsp::StateVariableTPTFilterType::bandpass);
        dryScratch.setSize(2, blockSize, false, true, false);
    }
    void reset() override { svf.reset(); lastCutoffHz = -1.0f; lastResonance = -1.0f; }

    void process(juce::AudioBuffer<float>& buf, int numSamples, int numChannels,
                 float cutoffHz, float resonance) override
    {
        // Skip the coefficient recompute when cutoff/resonance are unchanged.
        if (cutoffHz != lastCutoffHz || resonance != lastResonance)
        {
            svf.setCutoffFrequency(cutoffHz);
            svf.setResonance(juce::jmax(0.01f, resonance));
            lastCutoffHz = cutoffHz; lastResonance = resonance;
        }

        const int nCh = juce::jmin(numChannels, 2);
        for (int ch = 0; ch < nCh; ++ch)
            dryScratch.copyFrom(ch, 0, buf, ch, 0, numSamples);

        juce::dsp::AudioBlock<float> block(buf.getArrayOfWritePointers(),
                                           static_cast<size_t>(numChannels), 0,
                                           static_cast<size_t>(numSamples));
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        svf.process(ctx);

        for (int ch = 0; ch < nCh; ++ch)
        {
            auto*       data = buf.getWritePointer(ch);
            const auto* dry  = dryScratch.getReadPointer(ch);
            for (int i = 0; i < numSamples; ++i)
                data[i] = dry[i] - data[i];
        }
    }

private:
    juce::dsp::StateVariableTPTFilter<float> svf;
    juce::AudioBuffer<float> dryScratch;
    float lastCutoffHz = -1.0f, lastResonance = -1.0f;
};
