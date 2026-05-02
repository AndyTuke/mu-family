#pragma once

#include "EffectAlgorithmBase.h"
#include <cmath>

class BitcrushEffect : public EffectAlgorithmBase
{
public:
    BitcrushEffect()
    {
        def = FXAlgorithmRegistry::effectAlgorithms()[3];
    }

    const FXAlgorithmDef& getDef() const override { return def; }

    void prepareInner(double sampleRate, int blockSize) override
    {
        sr = sampleRate;
        juce::dsp::ProcessSpec spec{ sampleRate, (juce::uint32)blockSize, 2 };
        toneFilter.prepare(spec);
        updateToneFilter();
    }

    void processInner(juce::dsp::AudioBlock<float>& block) override
    {
        const float outGain  = juce::Decibels::decibelsToGain(outputDb);
        const float levels   = std::pow(2.0f, bits) - 1.0f;

        // Rate reduction: skip re-processing if rateCounter hasn't reached threshold.
        // rate=0 means no reduction (every sample), rate=1 means max reduction.
        const float rateThreshold = 1.0f + rate * 63.0f;

        for (size_t ch = 0; ch < block.getNumChannels(); ++ch)
        {
            auto* data = block.getChannelPointer(ch);
            float held = 0.0f;
            float counter = 0.0f;

            for (size_t i = 0; i < block.getNumSamples(); ++i)
            {
                counter += 1.0f;
                if (counter >= rateThreshold)
                {
                    held    = std::round(data[i] * levels) / levels;
                    counter = 0.0f;
                }
                data[i] = held * outGain;
            }
        }

        toneFilter.process(juce::dsp::ProcessContextReplacing<float>(block));
    }

    void setParam(const juce::String& id, float value) override
    {
        if      (id == "bits")   bits     = value;
        else if (id == "rate")   rate     = value / 100.0f;
        else if (id == "output") outputDb = value;
        else if (id == "tone")   { toneCutoff = value; updateToneFilter(); }
    }

private:
    void updateToneFilter()
    {
        *toneFilter.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, toneCutoff);
    }

    FXAlgorithmDef def;

    float bits       = 16.0f;
    float rate       = 0.0f;
    float outputDb   = 0.0f;
    float toneCutoff = 8000.0f;
    double sr        = 44100.0;

    juce::dsp::ProcessorDuplicator<
        juce::dsp::IIR::Filter<float>,
        juce::dsp::IIR::Coefficients<float>> toneFilter;
};
