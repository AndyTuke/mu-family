#pragma once

#include "FilterAlgorithmBase.h"
#include "Audio/AudioFilters.h"
#include <juce_dsp/juce_dsp.h>
#include <vector>

// The shared bodies of the filter algorithms that differ only in one setting. Each filter file
// names its type and says what it does; the processing lives here once.

// 12 dB/oct state-variable filter (JUCE StateVariableTPTFilter) of one response type.
template <juce::dsp::StateVariableTPTFilterType Type>
class SvfFilter12 : public FilterAlgorithmBase
{
public:
    void prepare(double sampleRate, int blockSize, int numChannels) override
    {
        const auto chans = static_cast<juce::uint32>(juce::jlimit(1, 2, numChannels));
        svf.prepare({ sampleRate, static_cast<juce::uint32>(blockSize), chans });
        svf.setType(Type);
    }
    void reset() override { svf.reset(); lastCutoffHz = -1.0f; lastResonance = -1.0f; }

    void process(juce::AudioBuffer<float>& buf, int numSamples, int numChannels,
                 float cutoffHz, float resonance) override
    {
        // Skip the coefficient recompute when cutoff/resonance are unchanged.
        if (cutoffHz != lastCutoffHz || resonance != lastResonance)
        {
            svf.setCutoffFrequency(cutoffHz);
            svf.setResonance(mu_filter::svfResonanceToQ(resonance));
            lastCutoffHz = cutoffHz; lastResonance = resonance;
        }
        juce::dsp::AudioBlock<float> block(buf.getArrayOfWritePointers(),
                                           static_cast<size_t>(numChannels), 0,
                                           static_cast<size_t>(numSamples));
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        svf.process(ctx);
    }

private:
    juce::dsp::StateVariableTPTFilter<float> svf;
    float lastCutoffHz = -1.0f, lastResonance = -1.0f;
};

// 24 dB/oct Moog-style ladder (JUCE LadderFilter) in one mode.
template <juce::dsp::LadderFilterMode Mode>
class LadderFilter24 : public FilterAlgorithmBase
{
public:
    void prepare(double sampleRate, int blockSize, int numChannels) override
    {
        const auto chans = static_cast<juce::uint32>(juce::jlimit(1, 2, numChannels));
        ladder.prepare({ sampleRate, static_cast<juce::uint32>(blockSize), chans });
        ladder.setDrive(1.0f);
        ladder.setMode(Mode);
    }
    void reset() override { ladder.reset(); lastCutoffHz = -1.0f; lastResonance = -1.0f; }

    void process(juce::AudioBuffer<float>& buf, int numSamples, int numChannels,
                 float cutoffHz, float resonance) override
    {
        // Skip the coefficient recompute when cutoff/resonance are unchanged.
        if (cutoffHz != lastCutoffHz || resonance != lastResonance)
        {
            ladder.setCutoffFrequencyHz(cutoffHz);
            ladder.setResonance(juce::jmax(0.01f, resonance));
            lastCutoffHz = cutoffHz; lastResonance = resonance;
        }
        juce::dsp::AudioBlock<float> block(buf.getArrayOfWritePointers(),
                                           static_cast<size_t>(numChannels), 0,
                                           static_cast<size_t>(numSamples));
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        ladder.process(ctx);
    }

private:
    juce::dsp::LadderFilter<float> ladder;
    float lastCutoffHz = -1.0f, lastResonance = -1.0f;
};

// A biquad per channel (the project's BiquadFilter) whose coefficients `SetCoefficients(f, hz,
// q, sampleRate)` sets — recomputed only when cutoff / resonance move; Q = 0.1 + resonance × 9.9.
template <void (*SetCoefficients)(BiquadFilter&, float, float, float)>
class StereoBiquadFilter : public FilterAlgorithmBase
{
public:
    void prepare(double sampleRate, int /*blockSize*/, int /*numChannels*/) override
    {
        currentSampleRate = sampleRate;
        reset();
    }
    void reset() override
    {
        eq[0].reset();
        eq[1].reset();
        lastCutoffHz  = -1.0f;
        lastResonance = -1.0f;
    }

    void process(juce::AudioBuffer<float>& buf, int numSamples, int numChannels,
                 float cutoffHz, float resonance) override
    {
        const bool dirty = (cutoffHz != lastCutoffHz) || (resonance != lastResonance);
        if (dirty)
        {
            const float q = 0.1f + resonance * 9.9f;   // 0..0.99 → 0.1..9.9
            for (int ch = 0; ch < 2; ++ch)
                SetCoefficients(eq[ch], cutoffHz, q, (float) currentSampleRate);
            lastCutoffHz  = cutoffHz;
            lastResonance = resonance;
        }
        const int nCh = juce::jmin(numChannels, 2);
        for (int ch = 0; ch < nCh; ++ch)
        {
            auto* data = buf.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i)
                data[i] = eq[ch].process(data[i]);
        }
    }

private:
    double       currentSampleRate = 44100.0;
    BiquadFilter eq[2];
    float        lastCutoffHz  = -1.0f;
    float        lastResonance = -1.0f;
};

// Feedback comb: y[n] = x[n] + Sign·g·y[n-D], D = sample_rate / cutoffHz. The delay buffer is
// sized for the lowest pitch (20 Hz cutoff floor) and the read tap linearly interpolates, so
// the resonant frequency is continuous. Resonance (g) and delay length are smoothed.
template <int Sign>
class CombFeedbackFilter : public FilterAlgorithmBase
{
public:
    void prepare(double sampleRate, int blockSize, int /*numChannels*/) override
    {
        currentSampleRate = sampleRate;
        const int maxCombSamples = static_cast<int>(sampleRate / 20.0) + 4;
        for (int ch = 0; ch < 2; ++ch)
        {
            buf[ch].assign(maxCombSamples, 0.0f);
            wPos[ch] = 0;
        }
        smoothedRes.reset(sampleRate, 0.005);  // 5 ms — eliminates resonance-knob crackle
        smoothedRes.setCurrentAndTargetValue(0.0f);
        // Delay length (= cutoff) is smoothed too: an abrupt cutoff change (e.g. a
        // stepped modulator) would jump the read tap and click. Ramping glides the
        // resonant pitch instead. 10 ms is short relative to a modulator step.
        smoothedDelay.reset(sampleRate, 0.010);
        smoothedDelay.setCurrentAndTargetValue(static_cast<float>(sampleRate / 1000.0));
        delayRamp.assign(static_cast<size_t>(juce::jmax(1, blockSize)), smoothedDelay.getCurrentValue());
    }
    void reset() override
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            std::fill(buf[ch].begin(), buf[ch].end(), 0.0f);
            wPos[ch] = 0;
        }
        smoothedDelay.setCurrentAndTargetValue(smoothedDelay.getTargetValue());  // no glide on reset
    }

    void process(juce::AudioBuffer<float>& audio, int numSamples, int numChannels,
                 float cutoffHz, float resonance) override
    {
        smoothedRes.setTargetValue(resonance);
        smoothedDelay.setTargetValue(static_cast<float>(currentSampleRate) / juce::jmax(20.0f, cutoffHz));
        // Pre-compute the per-sample delay ramp once so every channel reads the
        // identical glide (otherwise only ch0 advances it and ch1 would click).
        jassert(numSamples <= static_cast<int>(delayRamp.size()));
        for (int i = 0; i < numSamples; ++i)
            delayRamp[static_cast<size_t>(i)] = smoothedDelay.getNextValue();

        const int nCh = juce::jmin(numChannels, 2);
        for (int ch = 0; ch < nCh; ++ch)
        {
            auto&     b       = buf[ch];
            int&      w       = wPos[ch];
            const int bufSize = static_cast<int>(b.size());
            auto*     data    = audio.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i)
            {
                // ch0 advances the resonance ramp; ch1 reads the latest value.
                const float g     = (float) Sign * ((ch == 0) ? smoothedRes.getNextValue()
                                                          : smoothedRes.getCurrentValue());
                const float delayF = delayRamp[static_cast<size_t>(i)];
                const float readF = static_cast<float>(w) - delayF;
                const int   r0    = ((static_cast<int>(std::floor(readF)) % bufSize) + bufSize) % bufSize;
                const int   r1    = (r0 + 1) % bufSize;
                const float frac  = readF - std::floor(readF);
                const float delayed = b[r0] + frac * (b[r1] - b[r0]);
                const float out   = data[i] + g * delayed;
                b[w] = out;
                data[i] = out;
                w = (w + 1) % bufSize;
            }
        }
    }

private:
    double             currentSampleRate = 44100.0;
    std::vector<float> buf[2];
    int                wPos[2] = { 0, 0 };
    std::vector<float> delayRamp;   // per-sample smoothed delay length, shared across channels
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedRes;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedDelay;
};
