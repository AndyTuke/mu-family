#pragma once

#include "FilterAlgorithmBase.h"
#include <juce_dsp/juce_dsp.h>

// #427: type 10 — 24 dB/oct notch synthesised as dry minus bandpass via the
// JUCE LadderFilter. Same dry-subtract trick as Notch12Filter but at a
// steeper slope thanks to the ladder topology.
class Notch24Filter : public FilterAlgorithmBase
{
public:
    void prepare(double sampleRate, int blockSize, int numChannels) override
    {
        const auto chans = static_cast<juce::uint32>(juce::jlimit(1, 2, numChannels));
        ladder.prepare({ sampleRate, static_cast<juce::uint32>(blockSize), chans });
        ladder.setDrive(1.0f);
        ladder.setMode(juce::dsp::LadderFilter<float>::Mode::BPF24);
        dryScratch.setSize(2, blockSize, false, true, false);
    }
    void reset() override { ladder.reset(); }

    void process(juce::AudioBuffer<float>& buf, int numSamples, int numChannels,
                 float cutoffHz, float resonance) override
    {
        ladder.setCutoffFrequencyHz(cutoffHz);
        ladder.setResonance(juce::jmax(0.01f, resonance));

        const int nCh = juce::jmin(numChannels, 2);
        for (int ch = 0; ch < nCh; ++ch)
            dryScratch.copyFrom(ch, 0, buf, ch, 0, numSamples);

        juce::dsp::AudioBlock<float> block(buf.getArrayOfWritePointers(),
                                           static_cast<size_t>(numChannels), 0,
                                           static_cast<size_t>(numSamples));
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        ladder.process(ctx);

        for (int ch = 0; ch < nCh; ++ch)
        {
            auto*       data = buf.getWritePointer(ch);
            const auto* dry  = dryScratch.getReadPointer(ch);
            for (int i = 0; i < numSamples; ++i)
                data[i] = dry[i] - data[i];
        }
    }

private:
    juce::dsp::LadderFilter<float> ladder;
    juce::AudioBuffer<float> dryScratch;
};
