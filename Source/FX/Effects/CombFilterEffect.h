#pragma once

#include "EffectAlgorithmBase.h"
#include <vector>

class CombFilterEffect : public EffectAlgorithmBase
{
public:
    static constexpr int MaxDelaySamples = 96000;

    CombFilterEffect()
    {
        def = FXAlgorithmRegistry::effectAlgorithms()[7];
    }

    const FXAlgorithmDef& getDef() const override { return def; }

    void prepareInner(double sampleRate, int /*blockSize*/) override
    {
        sr = sampleRate;
        bufL.assign(MaxDelaySamples, 0.0f);
        bufR.assign(MaxDelaySamples, 0.0f);
        writePosL = writePosR = 0;
        updateDelaySamples();
    }

    void processInner(juce::dsp::AudioBlock<float>& block) override
    {
        const float fb      = feedbackAmt * 0.99f;
        const float outGain = juce::Decibels::decibelsToGain(outputDb);
        const float wet     = sendMode ? 1.0f : mix;
        const float dry     = sendMode ? 0.0f : 1.0f - mix;

        const size_t numSamples  = block.getNumSamples();
        const size_t numChannels = block.getNumChannels();

        auto* dataL = (numChannels > 0) ? block.getChannelPointer(0) : nullptr;
        auto* dataR = (numChannels > 1) ? block.getChannelPointer(1) : dataL;

        for (size_t i = 0; i < numSamples; ++i)
        {
            const float inL = (dataL != nullptr) ? dataL[i] : 0.0f;
            const float inR = (dataR != nullptr) ? dataR[i] : 0.0f;

            const int readPosL = (writePosL - delaySamples + MaxDelaySamples) % MaxDelaySamples;
            const int readPosR = (writePosR - delaySamples + MaxDelaySamples) % MaxDelaySamples;

            const float combL = bufL[readPosL];
            const float combR = bufR[readPosR];

            bufL[writePosL] = inL + combL * fb;
            bufR[writePosR] = inR + combR * fb;

            writePosL = (writePosL + 1) % MaxDelaySamples;
            writePosR = (writePosR + 1) % MaxDelaySamples;

            if (dataL != nullptr) dataL[i] = (dry * inL + wet * combL) * outGain;
            if (dataR != nullptr) dataR[i] = (dry * inR + wet * combR) * outGain;
        }
    }

    void setParam(const juce::String& id, float value) override
    {
        if      (id == "freq")     { freq        = value; updateDelaySamples(); }
        else if (id == "feedback") feedbackAmt   = value / 100.0f;
        else if (id == "output")   outputDb      = value;
        else if (id == "mix")      mix           = value / 100.0f;
    }

private:
    void updateDelaySamples()
    {
        delaySamples = juce::jlimit(1, MaxDelaySamples, static_cast<int>(sr / freq));
    }

    FXAlgorithmDef def;

    float  freq        = 500.0f;
    float  feedbackAmt = 0.5f;
    float  outputDb    = 0.0f;
    float  mix         = 0.5f;
    double sr          = 44100.0;
    int    delaySamples = 88;

    std::vector<float> bufL, bufR;
    int writePosL = 0, writePosR = 0;
};
