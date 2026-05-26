#pragma once

#include "EffectAlgorithmBase.h"
#include <cmath>
#include <vector>

// Stereo flanger: single modulated delay line (0.5–10ms) with feedback.
// Bipolar feedback: positive = flanging character, negative = inverted comb.
class FlangerEffect : public EffectAlgorithmBase
{
public:
    static constexpr int MaxDelaySamples = 2048;   // ~46ms at 44.1kHz, plenty for 10ms

    FlangerEffect()
    {
        def = FXAlgorithmRegistry::effectAlgorithms()[1];
    }

    const FXAlgorithmDef& getDef() const override { return def; }

    void prepareInner(double sampleRate, int /*blockSize*/) override
    {
        sr = sampleRate;
        bufL.assign(MaxDelaySamples, 0.0f);
        bufR.assign(MaxDelaySamples, 0.0f);
        writePos = 0;
        lfoPhase = 0.0f;
        feedL = feedR = 0.0f;
        // 15 ms ramps eliminate per-block step crackle on knob movement.
        smoothedRate    .reset(sr, 0.015);  smoothedRate    .setCurrentAndTargetValue(rate);
        smoothedDepth   .reset(sr, 0.015);  smoothedDepth   .setCurrentAndTargetValue(depth);
        smoothedFeedback.reset(sr, 0.015);  smoothedFeedback.setCurrentAndTargetValue(feedback);
        smoothedMix     .reset(sr, 0.015);  smoothedMix     .setCurrentAndTargetValue(mix);
    }

    void processInner(juce::dsp::AudioBlock<float>& block) override
    {
        // Centre = 5.25ms; depth sweeps ±baseSamp so the wet path can cross
        // the dry delay (through-zero), eliminating the comb at lfoVal=0.
        const float baseSamp = static_cast<float>(5.25 * 0.001 * sr);
        smoothedRate    .setTargetValue(rate);
        smoothedDepth   .setTargetValue(depth);
        smoothedFeedback.setTargetValue(feedback);
        smoothedMix     .setTargetValue(mix);

        const size_t numSamples  = block.getNumSamples();
        const size_t numChannels = block.getNumChannels();

        auto* dataL = (numChannels > 0) ? block.getChannelPointer(0) : nullptr;
        auto* dataR = (numChannels > 1) ? block.getChannelPointer(1) : dataL;

        for (size_t i = 0; i < numSamples; ++i)
        {
            const float rateNow  = smoothedRate    .getNextValue();
            const float depthNow = smoothedDepth   .getNextValue();
            const float fb       = smoothedFeedback.getNextValue();  // already ±0.95-scaled in setParam
            const float mixNow   = smoothedMix     .getNextValue();
            const float wet      = sendMode ? 1.0f : mixNow;
            const float dry      = sendMode ? 0.0f : 1.0f - mixNow;
            const float lfoInc   = rateNow / static_cast<float>(sr);
            const float depSamp  = baseSamp * depthNow;

            const float lfoVal    = std::sin(lfoPhase * juce::MathConstants<float>::twoPi);
            const float wetDelayS = juce::jmax(1.0f, baseSamp + lfoVal * depSamp);

            const float inL = dataL ? dataL[i] : 0.0f;
            const float inR = dataR ? dataR[i] : 0.0f;

            bufL[writePos] = inL + feedL * fb;
            bufR[writePos] = inR + feedR * fb;

            // Dry reads fixed base delay; wet reads LFO-swept delay.
            // Both come from the delay buffer so the comb passes through zero.
            const float dryL = readDelay(bufL, writePos, baseSamp);
            const float dryR = readDelay(bufR, writePos, baseSamp);
            const float wetL = readDelay(bufL, writePos, wetDelayS);
            const float wetR = readDelay(bufR, writePos, wetDelayS);
            feedL = wetL;
            feedR = wetR;

            if (dataL) dataL[i] = dry * dryL + wet * wetL;
            if (dataR) dataR[i] = dry * dryR + wet * wetR;

            writePos = (writePos + 1) % MaxDelaySamples;
            lfoPhase += lfoInc;
            if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;
        }
    }

    void setParam(const juce::String& id, float value) override
    {
        if      (id == "rate")     rate     = value;
        else if (id == "depth")    depth    = value / 100.0f;
        else if (id == "feedback") feedback = (value / 100.0f) * 0.95f;   // cap to prevent runaway
        else if (id == "mix")      mix      = value / 100.0f;
    }

private:
    static float readDelay(const std::vector<float>& buf, int writeP, float delaySamples)
    {
        const int bufSize = static_cast<int>(buf.size());
        const int d       = static_cast<int>(delaySamples);
        const float alpha = delaySamples - static_cast<float>(d);
        const int r0      = (writeP - d + bufSize) % bufSize;
        const int r1      = (r0 - 1 + bufSize) % bufSize;
        return buf[r0] * (1.0f - alpha) + buf[r1] * alpha;
    }

    FXAlgorithmDef def;
    double sr       = 44100.0;
    float rate      = 0.5f;
    float depth     = 0.5f;
    float feedback  = 0.0f;
    float mix       = 0.5f;

    std::vector<float> bufL, bufR;
    int   writePos  = 0;
    float lfoPhase  = 0.0f;
    float feedL     = 0.0f;
    float feedR     = 0.0f;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedRate;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedDepth;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedFeedback;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedMix;
};
