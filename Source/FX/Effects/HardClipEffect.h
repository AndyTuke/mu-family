#pragma once

#include "EffectAlgorithmBase.h"

class HardClipEffect : public EffectAlgorithmBase
{
public:
    HardClipEffect()
    {
        def = FXAlgorithmRegistry::effectAlgorithms()[1];
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
        const float driveGain = 1.0f + drive * 15.0f;
        const float clip      = juce::Decibels::decibelsToGain(thresholdDb);
        const float outGain   = juce::Decibels::decibelsToGain(outputDb);

        for (size_t ch = 0; ch < block.getNumChannels(); ++ch)
        {
            auto* data = block.getChannelPointer(ch);
            for (size_t i = 0; i < block.getNumSamples(); ++i)
                data[i] = juce::jlimit(-clip, clip, data[i] * driveGain) * outGain;
        }

        toneFilter.process(juce::dsp::ProcessContextReplacing<float>(block));
    }

    void setParam(const juce::String& id, float value) override
    {
        if      (id == "drive")     drive       = value;
        else if (id == "threshold") thresholdDb = value;
        else if (id == "output")    outputDb    = value;
        else if (id == "tone")      { toneCutoff = value; updateToneFilter(); }
    }

private:
    void updateToneFilter()
    {
        *toneFilter.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, toneCutoff);
    }

    FXAlgorithmDef def;

    float drive       = 0.5f;
    float thresholdDb = -6.0f;
    float outputDb    = 0.0f;
    float toneCutoff  = 8000.0f;
    double sr         = 44100.0;

    juce::dsp::ProcessorDuplicator<
        juce::dsp::IIR::Filter<float>,
        juce::dsp::IIR::Coefficients<float>> toneFilter;
};
