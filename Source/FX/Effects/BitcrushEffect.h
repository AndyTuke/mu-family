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
        aaState[0] = aaState[1] = 0.0f;
        updateAaCoeff();
    }

    void processInner(juce::dsp::AudioBlock<float>& block) override
    {
        const float outGain       = juce::Decibels::decibelsToGain(outputDb);
        const float levels        = std::pow(2.0f, bits) - 1.0f;
        const float lsb           = levels > 0.0f ? 1.0f / levels : 1.0f;
        const float rateThreshold = 1.0f + rate * 63.0f;

        for (size_t ch = 0; ch < block.getNumChannels(); ++ch)
        {
            auto* data    = block.getChannelPointer(ch);
            float held    = 0.0f;
            float counter = 0.0f;
            float aaS     = aaState[ch < 2 ? ch : 1];

            for (size_t i = 0; i < block.getNumSamples(); ++i)
            {
                // Pre-filter: 1-pole LP at new Nyquist (sr / 2*rateThreshold)
                aaS = (1.0f - aaCoeff) * data[i] + aaCoeff * aaS;

                counter += 1.0f;
                if (counter >= rateThreshold)
                {
                    // TPDF dither before quantisation: two uniform randoms → triangular PDF
                    const float r1  = rng.nextFloat();
                    const float r2  = rng.nextFloat();
                    held    = std::round((aaS + (r1 - r2) * lsb) * levels) / levels;
                    counter = 0.0f;
                }
                data[i] = held * outGain;
            }

            aaState[ch < 2 ? ch : 1] = aaS;
        }

        toneFilter.process(juce::dsp::ProcessContextReplacing<float>(block));
    }

    void setParam(const juce::String& id, float value) override
    {
        if      (id == "bits")   bits     = value;
        else if (id == "rate")   { rate = value / 100.0f; updateAaCoeff(); }
        else if (id == "output") outputDb = value;
        else if (id == "tone")   { toneCutoff = value; updateToneFilter(); }
    }

private:
    void updateToneFilter()
    {
        *toneFilter.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, toneCutoff);
    }

    // Anti-alias LP coefficient for 1-pole IIR at new Nyquist (sr / 2*rateThreshold).
    // coeff = exp(-pi / rateThreshold).
    void updateAaCoeff()
    {
        const float rateThreshold = 1.0f + rate * 63.0f;
        aaCoeff = std::exp(-juce::MathConstants<float>::pi / rateThreshold);
    }

    FXAlgorithmDef def;

    float bits       = 16.0f;
    float rate       = 0.0f;
    float outputDb   = 0.0f;
    float toneCutoff = 8000.0f;
    double sr        = 44100.0;
    float aaState[2] = {};
    float aaCoeff    = 0.0f;
    juce::Random rng;

    juce::dsp::ProcessorDuplicator<
        juce::dsp::IIR::Filter<float>,
        juce::dsp::IIR::Coefficients<float>> toneFilter;
};
