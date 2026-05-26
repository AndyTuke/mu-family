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
        // 15 ms ramps on all four params. Smoothing timeMs forces a fractional-
        // delay read but produces classic tape-echo "varispeed" pitch shift during
        // the ramp instead of a hard click — far less objectionable.
        smoothedTime    .reset(sr, 0.015);  smoothedTime    .setCurrentAndTargetValue(timeMs);
        smoothedFeedback.reset(sr, 0.015);  smoothedFeedback.setCurrentAndTargetValue(feedback);
        smoothedSpread  .reset(sr, 0.015);  smoothedSpread  .setCurrentAndTargetValue(spread);
        smoothedMix     .reset(sr, 0.015);  smoothedMix     .setCurrentAndTargetValue(mix);
    }

    void processInner(juce::dsp::AudioBlock<float>& block) override
    {
        smoothedTime    .setTargetValue(timeMs);
        smoothedFeedback.setTargetValue(feedback);
        smoothedSpread  .setTargetValue(spread);
        smoothedMix     .setTargetValue(mix);

        const size_t numSamples  = block.getNumSamples();
        const size_t numChannels = block.getNumChannels();

        auto* dataL = (numChannels > 0) ? block.getChannelPointer(0) : nullptr;
        auto* dataR = (numChannels > 1) ? block.getChannelPointer(1) : dataL;

        for (size_t i = 0; i < numSamples; ++i)
        {
            const float timeNow   = smoothedTime    .getNextValue();
            const float fb        = smoothedFeedback.getNextValue();
            const float spreadNow = smoothedSpread  .getNextValue();
            const float mixNow    = smoothedMix     .getNextValue();
            const float wet       = sendMode ? 1.0f : mixNow;
            const float dry       = sendMode ? 0.0f : 1.0f - mixNow;

            // Fractional-sample delay (linear interpolation between adjacent reads).
            const float delaySL = juce::jlimit(1.0f, (float)(MaxDelaySamples - 1),
                                               timeNow * 0.001f * (float)sr);
            const float delaySR = juce::jlimit(1.0f, (float)(MaxDelaySamples - 1),
                                               timeNow * 0.001f * (float)sr * (1.0f + spreadNow * 0.1f));

            const float inL = dataL ? dataL[i] : 0.0f;
            const float inR = dataR ? dataR[i] : 0.0f;

            const float echoL = readFracDelay(bufL, writePos, delaySL);
            const float echoR = readFracDelay(bufR, writePos, delaySR);

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
    // Linear-interpolated read at a fractional delay offset (samples).
    static float readFracDelay(const std::vector<float>& buf, int writeP, float delaySamples)
    {
        const int bufSize = static_cast<int>(buf.size());
        const int d       = static_cast<int>(delaySamples);
        const float alpha = delaySamples - static_cast<float>(d);
        const int r0      = (writeP - d + bufSize) % bufSize;
        const int r1      = (r0 - 1 + bufSize) % bufSize;
        return buf[r0] * (1.0f - alpha) + buf[r1] * alpha;
    }

    FXAlgorithmDef def;
    double sr      = 44100.0;
    float timeMs   = 250.0f;
    float feedback = 0.3f;
    float spread   = 0.0f;
    float mix      = 0.5f;

    std::vector<float> bufL, bufR;
    int writePos = 0;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedTime;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedFeedback;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedSpread;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedMix;
};
