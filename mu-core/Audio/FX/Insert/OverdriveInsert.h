#pragma once

#include "InsertAlgorithmBase.h"
#include "Audio/AudioFilters.h"
#include <cmath>

// insertAlgo = 14. Tube-style overdrive: pre-gain → biased tanh → DC block → tone LP → output trim.
// Bias shifts the signal off the curve's centre before the soft clip, so the clipping is asymmetric and adds even
// harmonics, the warm part of a tube stage; Bias 0 is the symmetric curve. The saturator is first-order
// anti-derivative anti-aliased (ADAA) like SoftClip, on the biased input (Parker et al. 2016, "Reducing the aliasing
// of nonlinear waveshaping using continuous-time convolution"). The static offset tanh(bias) and any drift from the
// asymmetry are removed, the first by subtraction and the rest by the DC block.
class OverdriveInsert : public InsertAlgorithmBase
{
public:
    void prepare(double sampleRate, int) override
    {
        currentSampleRate = sampleRate;
        smoothedPreGain.reset(sampleRate, 0.015);  smoothedPreGain.setCurrentAndTargetValue(1.0f);
        smoothedOutGain.reset(sampleRate, 0.015);  smoothedOutGain.setCurrentAndTargetValue(1.0f);
        smoothedBias.reset(sampleRate, 0.015);     smoothedBias.setCurrentAndTargetValue(0.0f);
        reset();
    }
    void reset() override
    {
        toneFilter[0].reset();
        toneFilter[1].reset();
        dcIn [0] = dcIn [1] = 0.0f;
        dcOut[0] = dcOut[1] = 0.0f;
        xPrev[0] = xPrev[1] = 0.0f;
        // Snap the smoothers to their prepare() values so a reset slot ramps in exactly as a fresh one.
        smoothedPreGain.setCurrentAndTargetValue(1.0f);
        smoothedOutGain.setCurrentAndTargetValue(1.0f);
        smoothedBias.setCurrentAndTargetValue(0.0f);
    }

    void process(juce::AudioBuffer<float>& buf, int ns, int nCh,
                 const VoiceParams& p, float& /*grOut*/) override
    {
        const int nChState = juce::jmin(nCh, 2);   // state is kept for two channels; extra channels pass through
        // Slot 0 = Drive 0..100, Slot 1 = Output -24..0 dB, Slot 2 = Bias 0..100 %, Slot 3 = Tone 200..20000 Hz (log).
        const float drive    = insertSlot(p, 0);
        const float outputDb = insertSlot(p, 1);
        const float bias     = juce::jlimit(0.0f, 100.0f, insertSlot(p, 2)) / 100.0f * kMaxBias;
        const float toneHz   = juce::jlimit(200.0f, 20000.0f, insertSlot(p, 3));
        const float preGain  = std::pow(10.0f, drive / 100.0f * 2.0f);   // 1..100× (0..40 dB), as SoftClip
        // Square-root makeup, as SoftClip: full 1/preGain would make loud drive settings far quieter.
        const float outGain  = std::pow(10.0f, outputDb / 20.0f) / std::sqrt(preGain);
        const float dcCoeff  = 1.0f - (2.0f * juce::MathConstants<float>::pi * 20.0f / (float) currentSampleRate);
        smoothedPreGain.setTargetValue(preGain);
        smoothedOutGain.setTargetValue(outGain);
        smoothedBias.setTargetValue(bias);
        for (int ch = 0; ch < nChState; ++ch)
            toneFilter[ch].prepare(toneHz, (float) currentSampleRate);

        // ADAA's antiderivative of tanh, log(cosh x), with the large-|x| form that cannot overflow.
        auto ad1Tanh = [](float x) -> float {
            return std::abs(x) > 12.0f ? std::abs(x) - 0.6931472f : std::log(std::cosh(x));
        };
        // Sample-major: each smoothed value advances once per sample and every channel uses it.
        auto chans = buf.getArrayOfWritePointers();
        for (int i = 0; i < ns; ++i)
        {
            const float pg = smoothedPreGain.getNextValue();
            const float og = smoothedOutGain.getNextValue();
            const float b  = smoothedBias.getNextValue();
            const float restingOffset = std::tanh(b);   // what the curve outputs for silence at this bias
            for (int ch = 0; ch < nChState; ++ch)
            {
                float& xp = xPrev[ch];
                const float x  = chans[ch][i] * pg + b;
                const float dx = x - xp;
                const float sat = std::abs(dx) < 1e-4f ? std::tanh(0.5f * (x + xp))
                                                       : (ad1Tanh(x) - ad1Tanh(xp)) / dx;
                xp = x;
                const float centred = sat - restingOffset;
                float& di   = dcIn [ch];
                float& dout = dcOut[ch];
                const float dc = centred - di + dcCoeff * dout;
                di   = centred;
                dout = dc;
                chans[ch][i] = toneFilter[ch].process(dc) * og;
            }
        }
    }

private:
    static constexpr float kMaxBias = 0.6f;   // the offset at Bias 100 %, in units of the curve's knee

    double    currentSampleRate = 44100.0;
    OnePoleLP toneFilter[2];
    float     dcIn [2] = {};
    float     dcOut[2] = {};
    float     xPrev[2] = {};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedPreGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedOutGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedBias;
};
