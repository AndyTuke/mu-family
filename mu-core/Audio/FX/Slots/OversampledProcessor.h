#pragma once

#include <juce_dsp/juce_dsp.h>

// Wraps juce::dsp::Oversampling for a fixed channel count and oversampling factor.
// Factor 1 = bypass (no allocation). Factor 2 = 2x. Factor 4 = 4x.
// Usage: call prepare() once, then wrap your DSP inside process().
class OversampledProcessor
{
public:
    explicit OversampledProcessor(int factor, int numChannels = 2)
        : factor(factor), numChannels(numChannels)
    {
        jassert(factor == 1 || factor == 2 || factor == 4);
    }

    void prepare(double sampleRate, int blockSize)
    {
        baseRate = sampleRate;

        if (factor > 1)
        {
            int stages = (factor == 4) ? 2 : 1;  // log2: 1→2x, 2→4x
            oversampling = std::make_unique<juce::dsp::Oversampling<float>>(
                numChannels,
                stages,
                juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
                true);
            oversampling->initProcessing(static_cast<size_t>(blockSize));
        }
    }

    // Returns the sample rate at which the callback runs (baseRate * factor).
    double getOversampledRate() const { return baseRate * factor; }

    // Upsample, run callback on oversampled block, downsample back.
    // callback signature: void(juce::dsp::AudioBlock<float>&)
    template<typename Callback>
    void process(juce::AudioBuffer<float>& buffer, Callback&& callback)
    {
        if (factor == 1 || !oversampling)
        {
            auto block = juce::dsp::AudioBlock<float>(buffer);
            callback(block);
            return;
        }

        juce::dsp::AudioBlock<float> inputBlock(buffer);
        auto osBlock = oversampling->processSamplesUp(inputBlock);
        callback(osBlock);
        oversampling->processSamplesDown(inputBlock);
    }

private:
    int    factor;
    int    numChannels;
    double baseRate = 44100.0;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
};
