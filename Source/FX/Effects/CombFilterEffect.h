#pragma once

#include "EffectAlgorithmBase.h"
#include <vector>
#include <cmath>

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

        // 20 ms glide on delay-length changes eliminates clicks when sweeping cutoff.
        smoothCoeff = (float)std::exp(-1.0 / (0.020 * sampleRate));

        updateDelayTarget();
        smoothedDelayL = smoothedDelayR = targetDelaySamples;
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

            // Smooth delay length each sample to avoid discontinuities when freq changes.
            smoothedDelayL = smoothCoeff * smoothedDelayL + (1.0f - smoothCoeff) * targetDelaySamples;
            smoothedDelayR = smoothedDelayL;  // mono delay length, same for both channels

            // Linear interpolation for fractional delay read.
            const float combL = readInterp(bufL, writePosL, smoothedDelayL);
            const float combR = readInterp(bufR, writePosR, smoothedDelayR);

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
        if      (id == "freq")     { freq = value; updateDelayTarget(); }
        else if (id == "feedback") feedbackAmt = value / 100.0f;
        else if (id == "output")   outputDb    = value;
        else if (id == "mix")      mix         = value / 100.0f;
    }

private:
    static float readInterp(const std::vector<float>& buf, int writePos, float delaySamples)
    {
        const float d    = juce::jlimit(1.0f, (float)(MaxDelaySamples - 1), delaySamples);
        const int   di   = (int)d;
        const float frac = d - (float)di;
        const int   r0   = (writePos - di     + MaxDelaySamples) % MaxDelaySamples;
        const int   r1   = (writePos - di - 1 + MaxDelaySamples) % MaxDelaySamples;
        return buf[r0] * (1.0f - frac) + buf[r1] * frac;
    }

    void updateDelayTarget()
    {
        targetDelaySamples = juce::jlimit(1.0f, (float)MaxDelaySamples,
                                          (float)(sr / (double)freq));
    }

    FXAlgorithmDef def;

    float  freq           = 500.0f;
    float  feedbackAmt    = 0.5f;
    float  outputDb       = 0.0f;
    float  mix            = 0.5f;
    double sr             = 44100.0;

    float targetDelaySamples = 88.0f;
    float smoothedDelayL     = 88.0f;
    float smoothedDelayR     = 88.0f;
    float smoothCoeff        = 0.9f;

    std::vector<float> bufL, bufR;
    int writePosL = 0, writePosR = 0;
};
