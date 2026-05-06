#pragma once

#include "EffectAlgorithmBase.h"
#include <vector>

// Stereo echo: independent L/R delay lines, no BPM sync (use DelaySlot for sync).
// Spread offsets R delay by up to +10% for stereo width.
class EchoEffect : public EffectAlgorithmBase
{
public:
    // 500ms at 192kHz
    static constexpr int MaxDelaySamples = 96001;

    EchoEffect()
    {
        def = FXAlgorithmRegistry::effectAlgorithms()[3];
    }

    const FXAlgorithmDef& getDef() const override { return def; }

    void prepareInner(double sampleRate, int /*blockSize*/) override
    {
        sr = sampleRate;
        bufL.assign(MaxDelaySamples, 0.0f);
        bufR.assign(MaxDelaySamples, 0.0f);
        writePos = 0;
    }

    void processInner(juce::dsp::AudioBlock<float>& block) override
    {
        const int delayL = juce::jlimit(1, MaxDelaySamples - 1,
                                        static_cast<int>(timeMs * 0.001 * sr));
        const int delayR = juce::jlimit(1, MaxDelaySamples - 1,
                                        static_cast<int>(timeMs * 0.001 * sr * (1.0f + spread * 0.1f)));
        const float wet = sendMode ? 1.0f : mix;
        const float dry = sendMode ? 0.0f : 1.0f - mix;
        const float fb  = feedback;

        const size_t numSamples  = block.getNumSamples();
        const size_t numChannels = block.getNumChannels();

        auto* dataL = (numChannels > 0) ? block.getChannelPointer(0) : nullptr;
        auto* dataR = (numChannels > 1) ? block.getChannelPointer(1) : dataL;

        for (size_t i = 0; i < numSamples; ++i)
        {
            const float inL = dataL ? dataL[i] : 0.0f;
            const float inR = dataR ? dataR[i] : 0.0f;

            const int rL = (writePos - delayL + MaxDelaySamples) % MaxDelaySamples;
            const int rR = (writePos - delayR + MaxDelaySamples) % MaxDelaySamples;

            const float echoL = bufL[rL];
            const float echoR = bufR[rR];

            bufL[writePos] = inL + echoL * fb;
            bufR[writePos] = inR + echoR * fb;

            if (dataL) dataL[i] = dry * inL + wet * echoL;
            if (dataR) dataR[i] = dry * inR + wet * echoR;

            writePos = (writePos + 1) % MaxDelaySamples;
        }
    }

    void setParam(const juce::String& id, float value) override
    {
        if      (id == "time")     timeMs   = value;
        else if (id == "feedback") feedback = juce::jlimit(0.0f, 0.98f, value / 100.0f);
        else if (id == "spread")   spread   = juce::jlimit(0.0f, 1.0f, value / 100.0f);
        else if (id == "mix")      mix      = value / 100.0f;
    }

private:
    FXAlgorithmDef def;
    double sr      = 44100.0;
    float timeMs   = 250.0f;
    float feedback = 0.3f;
    float spread   = 0.0f;
    float mix      = 0.5f;

    std::vector<float> bufL, bufR;
    int writePos = 0;
};
