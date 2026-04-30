#pragma once

#include "EffectAlgorithmBase.h"
#include <cmath>

// Stereo chorus: multiple delay lines modulated by LFOs at slightly different rates.
class ChorusEffect : public EffectAlgorithmBase
{
public:
    static constexpr int MaxVoices = 4;
    static constexpr int MaxDelaySamples = 4096;

    ChorusEffect()
    {
        def = FXAlgorithmRegistry::effectAlgorithms()[5];
    }

    const FXAlgorithmDef& getDef() const override { return def; }

    void prepareInner(double sampleRate, int /*blockSize*/) override
    {
        sr = sampleRate;
        for (int v = 0; v < MaxVoices; ++v)
        {
            delayL[v].resize(MaxDelaySamples, 0.0f);
            delayR[v].resize(MaxDelaySamples, 0.0f);
            writePos[v] = 0;
            phase[v]    = static_cast<float>(v) / MaxVoices;
        }
    }

    void processInner(juce::dsp::AudioBlock<float>& block) override
    {
        const int   numVoices = juce::jlimit(2, MaxVoices, static_cast<int>(voices));
        const float lfoInc    = static_cast<float>(rate / sr);
        const float depthSamp = static_cast<float>(depth * 0.02 * sr);
        const float baseSamp  = static_cast<float>(0.03 * sr);  // 30ms base delay
        const float wet       = mix;
        const float dry       = 1.0f - mix;

        const size_t numSamples  = block.getNumSamples();
        const size_t numChannels = block.getNumChannels();

        auto* dataL = (numChannels > 0) ? block.getChannelPointer(0) : nullptr;
        auto* dataR = (numChannels > 1) ? block.getChannelPointer(1) : dataL;

        for (size_t i = 0; i < numSamples; ++i)
        {
            float wetL = 0.0f, wetR = 0.0f;
            const float inL = (dataL != nullptr) ? dataL[i] : 0.0f;
            const float inR = (dataR != nullptr) ? dataR[i] : 0.0f;

            for (int v = 0; v < numVoices; ++v)
            {
                delayL[v][writePos[v]] = inL;
                delayR[v][writePos[v]] = inR;

                const float lfoVal  = std::sin(phase[v] * juce::MathConstants<float>::twoPi);
                const float spreadV = (v % 2 == 0) ? 1.0f : (1.0f + spread * 0.5f);
                const float delaySL = baseSamp + lfoVal * depthSamp;
                const float delaySR = baseSamp + lfoVal * depthSamp * spreadV;

                wetL += readDelayLine(delayL[v], writePos[v], delaySL);
                wetR += readDelayLine(delayR[v], writePos[v], delaySR);

                phase[v] += lfoInc;
                if (phase[v] >= 1.0f) phase[v] -= 1.0f;

                writePos[v] = (writePos[v] + 1) % MaxDelaySamples;
            }

            const float scale = 1.0f / numVoices;
            if (dataL != nullptr) dataL[i] = dry * inL + wet * wetL * scale;
            if (dataR != nullptr) dataR[i] = dry * inR + wet * wetR * scale;
        }
    }

    void setParam(const juce::String& id, float value) override
    {
        if      (id == "rate")   rate   = value;
        else if (id == "depth")  depth  = value;
        else if (id == "voices") voices = value;
        else if (id == "spread") spread = value;
        else if (id == "mix")    mix    = value;
    }

private:
    static float readDelayLine(const std::vector<float>& buf, int writeP, float delaySamples)
    {
        const int bufSize = static_cast<int>(buf.size());
        const int frac    = static_cast<int>(delaySamples);
        const float alpha = delaySamples - frac;
        const int readP0  = (writeP - frac + bufSize) % bufSize;
        const int readP1  = (readP0 - 1 + bufSize) % bufSize;
        return buf[readP0] * (1.0f - alpha) + buf[readP1] * alpha;
    }

    FXAlgorithmDef def;

    float rate   = 1.0f;
    float depth  = 0.5f;
    float voices = 2.0f;
    float spread = 0.5f;
    float mix    = 0.5f;
    double sr    = 44100.0;

    std::vector<float> delayL[MaxVoices];
    std::vector<float> delayR[MaxVoices];
    int   writePos[MaxVoices] = {};
    float phase[MaxVoices]    = {};
};
