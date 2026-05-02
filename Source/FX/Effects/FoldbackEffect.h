#pragma once

#include "EffectAlgorithmBase.h"
#include <cmath>

class FoldbackEffect : public EffectAlgorithmBase
{
public:
    FoldbackEffect()
    {
        def = FXAlgorithmRegistry::effectAlgorithms()[2];
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
        const float driveGain = 1.0f + drive * 10.0f;
        const float outGain   = juce::Decibels::decibelsToGain(outputDb);
        const int   numFolds  = static_cast<int>(folds);

        for (size_t ch = 0; ch < block.getNumChannels(); ++ch)
        {
            auto* data = block.getChannelPointer(ch);
            for (size_t i = 0; i < block.getNumSamples(); ++i)
                data[i] = fold(data[i] * driveGain, numFolds) / driveGain * outGain;
        }

        toneFilter.process(juce::dsp::ProcessContextReplacing<float>(block));
    }

    void setParam(const juce::String& id, float value) override
    {
        if      (id == "drive")  drive    = value / 100.0f;
        else if (id == "folds")  folds    = value;
        else if (id == "output") outputDb = value;
        else if (id == "tone")   { toneCutoff = value; updateToneFilter(); }
    }

private:
    // Triangular foldback: folds the signal back when it exceeds ±1
    static float fold(float x, int numFolds)
    {
        for (int i = 0; i < numFolds; ++i)
        {
            if (x >  1.0f) x =  2.0f - x;
            if (x < -1.0f) x = -2.0f - x;
        }
        return x;
    }

    void updateToneFilter()
    {
        *toneFilter.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, toneCutoff);
    }

    FXAlgorithmDef def;

    float drive      = 0.5f;
    float folds      = 2.0f;
    float outputDb   = 0.0f;
    float toneCutoff = 8000.0f;
    double sr        = 44100.0;

    juce::dsp::ProcessorDuplicator<
        juce::dsp::IIR::Filter<float>,
        juce::dsp::IIR::Coefficients<float>> toneFilter;
};
