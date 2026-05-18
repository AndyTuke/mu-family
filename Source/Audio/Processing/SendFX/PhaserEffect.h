#pragma once

#include "EffectAlgorithmBase.h"
#include <cmath>
#include <array>

// Stereo phaser: allpass filter chain modulated by an LFO.
class PhaserEffect : public EffectAlgorithmBase
{
public:
    static constexpr int MaxStages = 12;

    PhaserEffect()
    {
        def = FXAlgorithmRegistry::effectAlgorithms()[2];
    }

    const FXAlgorithmDef& getDef() const override { return def; }

    void prepareInner(double sampleRate, int /*blockSize*/) override
    {
        sr = sampleRate;
        for (auto& s : stateL) s = 0.0f;
        for (auto& s : stateR) s = 0.0f;
        feedbackL = feedbackR = 0.0f;
        lfoPhase = 0.0f;
    }

    void processInner(juce::dsp::AudioBlock<float>& block) override
    {
        const int   numStages = juce::jlimit(2, MaxStages, (static_cast<int>(stages) / 2) * 2);
        const float lfoInc    = static_cast<float>(rate / sr);
        const float wet       = sendMode ? 1.0f : mix;
        const float dry       = sendMode ? 0.0f : 1.0f - mix;
        const float fb        = feedback * 0.99f;

        // Precompute constants for frequency-correct bilinear-transform coefficient.
        // Notch sweeps logarithmically from fMin to fMax as LFO goes -1 to +1.
        static constexpr float fMin = 200.0f, fMax = 4000.0f;
        const float logRange  = std::log(fMax / fMin);
        const float piOverSr  = juce::MathConstants<float>::pi / static_cast<float>(sr);

        const size_t numSamples  = block.getNumSamples();
        const size_t numChannels = block.getNumChannels();

        auto* dataL = (numChannels > 0) ? block.getChannelPointer(0) : nullptr;
        auto* dataR = (numChannels > 1) ? block.getChannelPointer(1) : dataL;

        for (size_t i = 0; i < numSamples; ++i)
        {
            const float lfoVal  = std::sin(lfoPhase * juce::MathConstants<float>::twoPi);
            // Map LFO to a logarithmic frequency sweep, then convert to allpass coeff.
            const float lfoNorm = 0.5f + 0.5f * lfoVal * depth;   // 0..1
            const float freq    = fMin * std::exp(lfoNorm * logRange);
            const float t       = std::tan(piOverSr * freq);
            const float coeff   = (1.0f - t) / (1.0f + t);

            const float inL = (dataL != nullptr) ? dataL[i] : 0.0f;
            const float inR = (dataR != nullptr) ? dataR[i] : 0.0f;

            float sigL = inL + feedbackL * fb;
            float sigR = inR + feedbackR * fb;

            for (int s = 0; s < numStages; ++s)
            {
                sigL = allpass(sigL, stateL[s], coeff);
                sigR = allpass(sigR, stateR[s], coeff);
            }

            feedbackL = sigL;
            feedbackR = sigR;

            if (dataL != nullptr) dataL[i] = dry * inL + wet * sigL;
            if (dataR != nullptr) dataR[i] = dry * inR + wet * sigR;

            lfoPhase += lfoInc;
            if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;
        }
    }

    void setParam(const juce::String& id, float value) override
    {
        if      (id == "rate")     rate     = value;
        else if (id == "depth")    depth    = value / 100.0f;
        else if (id == "stages")   stages   = value;
        else if (id == "feedback") feedback = value / 100.0f;
        else if (id == "mix")      mix      = value / 100.0f;
    }

private:
    static float allpass(float x, float& z, float coeff)
    {
        const float out = -coeff * x + z;
        z = x + coeff * out;
        return out;
    }

    FXAlgorithmDef def;

    float rate     = 0.5f;
    float depth    = 0.5f;
    float stages   = 6.0f;
    float feedback = 0.5f;
    float mix      = 0.5f;
    double sr      = 44100.0;

    std::array<float, MaxStages> stateL{};
    std::array<float, MaxStages> stateR{};
    float feedbackL = 0.0f;
    float feedbackR = 0.0f;
    float lfoPhase  = 0.0f;
};
