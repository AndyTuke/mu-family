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
        // Fractional sample delay so automating `time` produces smooth sweeps
        // instead of zipper artifacts. Range floored at 2 samples for Hermite
        // safety (the interpolator reads xm1/x0/x1/x2 around the integer base).
        const float delaySamplesL = juce::jlimit(2.0f, (float)(MaxDelaySamples - 2),
                                                 timeMs * 0.001f * (float)sr);
        const float delaySamplesR = juce::jlimit(2.0f, (float)(MaxDelaySamples - 2),
                                                 timeMs * 0.001f * (float)sr * (1.0f + spread * 0.1f));
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

            const float echoL = hermiteDelay(bufL, writePos, delaySamplesL);
            const float echoR = hermiteDelay(bufR, writePos, delaySamplesR);

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
    // 4-point Catmull-Rom Hermite interpolation. Matches the form used in
    // DelaySlot::hermiteDelay and ChorusEffect: xm1 = one sample newer than the
    // integer base, x0 = base, x1/x2 = older.
    static float hermiteDelay(const std::vector<float>& buf, int writeP, float delaySamples)
    {
        const int   bufSize = static_cast<int>(buf.size());
        const float d       = juce::jlimit(2.0f, (float)(bufSize - 2), delaySamples);
        const int   di      = static_cast<int>(d);
        const float frac    = d - (float)di;

        const int r0  = (writeP - di     + bufSize) % bufSize;
        const int rm1 = (writeP - di + 1 + bufSize) % bufSize;
        const int r1  = (writeP - di - 1 + bufSize) % bufSize;
        const int r2  = (writeP - di - 2 + bufSize) % bufSize;

        const float xm1 = buf[rm1];
        const float x0  = buf[r0];
        const float x1  = buf[r1];
        const float x2  = buf[r2];

        const float c0 = x0;
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);

        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    }

    FXAlgorithmDef def;
    double sr      = 44100.0;
    float timeMs   = 250.0f;
    float feedback = 0.3f;
    float spread   = 0.0f;
    float mix      = 0.5f;

    std::vector<float> bufL, bufR;
    int writePos = 0;
};
