#pragma once

#include "EffectAlgorithmBase.h"
#include <cmath>

// Stereo chorus: multiple delay lines modulated by LFOs at slightly different rates.
class ChorusEffect : public EffectAlgorithmBase
{
public:
    static constexpr int MaxVoices = 4;

    ChorusEffect()
    {
        def = FXAlgorithmRegistry::effectAlgorithms()[0];
    }

    const FXAlgorithmDef& getDef() const override { return def; }

    void prepareInner(double sampleRate, int /*blockSize*/) override
    {
        sr = sampleRate;
        // Size the delay lines for the worst-case read offset at THIS sample rate: the read
        // reaches baseSamp (0.03·sr) + depthSamp·spread (up to 0.03·sr) ≈ 0.06·sr behind the
        // write head, plus the Hermite read's +2 lookahead. 0.07·sr gives headroom so the
        // wrap modulo in hermiteDelay can never go negative (was a fixed 4096 → out-of-bounds
        // read above ~68 kHz, i.e. at 88.2/96/192 kHz).
        delaySize = juce::jmax(4096, (int) std::ceil(0.07 * sr));
        for (int v = 0; v < MaxVoices; ++v)
        {
            delayL[v].assign((size_t) delaySize, 0.0f);
            delayR[v].assign((size_t) delaySize, 0.0f);
            writePos[v] = 0;
            phase[v]    = static_cast<float>(v) / MaxVoices;
        }
        // 15 ms ramps on every continuous knob so block-to-block param steps
        // do not produce clicks or crackle.
        smoothedDepth .reset(sr, 0.015);  smoothedDepth .setCurrentAndTargetValue(depth);
        smoothedRate  .reset(sr, 0.015);  smoothedRate  .setCurrentAndTargetValue(rate);
        smoothedMix   .reset(sr, 0.015);  smoothedMix   .setCurrentAndTargetValue(mix);
        smoothedSpread.reset(sr, 0.015);  smoothedSpread.setCurrentAndTargetValue(spread);
    }

    void processInner(juce::dsp::AudioBlock<float>& block) override
    {
        const int    numVoices = juce::jlimit(2, MaxVoices, static_cast<int>(voices));
        const float  baseSamp  = static_cast<float>(0.03 * sr);  // 30ms base delay
        smoothedDepth .setTargetValue(depth);
        smoothedRate  .setTargetValue(rate);
        smoothedMix   .setTargetValue(mix);
        smoothedSpread.setTargetValue(spread);

        const size_t numSamples  = block.getNumSamples();
        const size_t numChannels = block.getNumChannels();

        auto* dataL = (numChannels > 0) ? block.getChannelPointer(0) : nullptr;
        auto* dataR = (numChannels > 1) ? block.getChannelPointer(1) : dataL;

        for (size_t i = 0; i < numSamples; ++i)
        {
            const float depthNow  = smoothedDepth .getNextValue();
            const float rateNow   = smoothedRate  .getNextValue();
            const float mixNow    = smoothedMix   .getNextValue();
            const float spreadNow = smoothedSpread.getNextValue();
            const float depthSamp = depthNow * 0.02f * static_cast<float>(sr);
            const float wet       = sendMode ? 1.0f : mixNow;
            const float dry       = sendMode ? 0.0f : 1.0f - mixNow;

            float wetL = 0.0f, wetR = 0.0f;
            const float inL = (dataL != nullptr) ? dataL[i] : 0.0f;
            const float inR = (dataR != nullptr) ? dataR[i] : 0.0f;

            for (int v = 0; v < numVoices; ++v)
            {
                delayL[v][writePos[v]] = inL;
                delayR[v][writePos[v]] = inR;

                const float lfoVal  = std::sin(phase[v] * juce::MathConstants<float>::twoPi);
                const float spreadV = (v % 2 == 0) ? 1.0f : (1.0f + spreadNow * 0.5f);
                const float delaySL = baseSamp + lfoVal * depthSamp;
                const float delaySR = baseSamp + lfoVal * depthSamp * spreadV;

                wetL += hermiteDelay(delayL[v], writePos[v], delaySL);
                wetR += hermiteDelay(delayR[v], writePos[v], delaySR);

                // Per-voice LFO detuning: evenly spread ±1.5% from base rate
                // so all voices average to the user-set rate.
                const float t      = numVoices > 1 ? (float)v / (numVoices - 1) : 0.5f;
                const float detune = 1.0f + (t - 0.5f) * 0.03f;
                phase[v] += rateNow * detune / static_cast<float>(sr);
                if (phase[v] >= 1.0f) phase[v] -= 1.0f;

                writePos[v] = (writePos[v] + 1) % delaySize;
            }

            const float scale = 1.0f / numVoices;
            if (dataL != nullptr) dataL[i] = dry * inL + wet * wetL * scale;
            if (dataR != nullptr) dataR[i] = dry * inR + wet * wetR * scale;
        }
    }

    void setParam(const juce::String& id, float value) override
    {
        if      (id == "rate")   rate   = value;
        else if (id == "depth")  depth  = value / 100.0f;
        else if (id == "voices") voices = value;
        else if (id == "spread") spread = value / 100.0f;
        else if (id == "mix")    mix    = value / 100.0f;
    }

private:
    // 4-point Catmull-Rom Hermite interpolation for smooth chorus modulation.
    static float hermiteDelay(const std::vector<float>& buf, int writeP, float delaySamples)
    {
        const int bufSize = static_cast<int>(buf.size());
        delaySamples = juce::jmax(2.0f, delaySamples);
        const int n   = static_cast<int>(delaySamples);
        const float t = delaySamples - n;

        auto rd = [&](int offset) -> float {
            return buf[(writeP - offset + bufSize) % bufSize];
        };

        const float xm1 = rd(n - 1);
        const float x0  = rd(n);
        const float x1  = rd(n + 1);
        const float x2  = rd(n + 2);

        const float a = -0.5f * xm1 + 1.5f * x0 - 1.5f * x1 + 0.5f * x2;
        const float b =         xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c = -0.5f * xm1              + 0.5f * x1;
        return ((a * t + b) * t + c) * t + x0;
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
    int   delaySize           = 4096;   // per-line length, sized from sr in prepareInner
    int   writePos[MaxVoices] = {};
    float phase[MaxVoices]    = {};

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedDepth;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedRate;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedMix;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedSpread;
};
