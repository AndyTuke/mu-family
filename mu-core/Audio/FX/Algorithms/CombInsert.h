#pragma once

#include "Audio/FX/Algorithms/InsertAlgorithmBase.h"
#include "Audio/FX/Algorithms/FxUse.h"
#include "Audio/FX/Algorithms/InsertSupport.h"
#include <cmath>
#include <vector>

namespace mu_audio
{
// insertAlgo = 25. Comb: a feedback comb filter on the incoming signal, so whatever passes through rings at one pitch. Unlike
// Karplus (a note-and-octave string excited by the input's attack), this colours the whole signal and takes a negative feedback.
//   - Freq      20..5000 Hz (log): the resonant pitch (the delay is the sample rate divided by Freq, minus the loop's own delay).
//   - Feedback  -95..95 %: loop gain; negative inverts the loop (odd harmonics only, hollow, an octave lower).
//   - Damping   0..100 %: a one-pole low-pass in the loop, 20 kHz open down to about 800 Hz; darker and shorter ringing.
//   - Mix       0..100 %: dry / wet; 0 leaves the signal untouched.
// y[n] = x[n] + g * damp(y[n - D]), wet = y * sqrt(1 - g^2) (power-normalised, so broadband input keeps about its level).
// The delay is read with 3rd-order Lagrange interpolation so the pitch is true to a fraction of a sample. No latency.
class CombInsert : public InsertAlgorithmBase
{
public:
    static constexpr FxUse kUse = FxUse::Insert;

    void prepare(double sampleRate, int) override
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        ringSize = (int) std::ceil(sr / 20.0) + 8;
        for (auto& r : ring) r.assign((size_t) ringSize, 0.0f);
        logFreqSm.reset(sr, 0.015);
        fbSm.reset(sr, 0.015);
        mixSm.reset(sr, 0.015);
        reset();
    }

    void reset() override
    {
        for (auto& r : ring) std::fill(r.begin(), r.end(), 0.0f);
        for (auto& l : lp) l = 0.0f;
        writePos = 0;
        primed = false;
    }

    void process(juce::AudioBuffer<float>& buf, int ns, int nCh,
                 const VoiceParams& p, float& /*grOut*/) override
    {
        using namespace insert_support;
        const int nChState = juce::jmin(nCh, 2);
        if (nChState <= 0 || ns <= 0 || ring[0].empty()) return;
        // Slot 0 = Freq, 1 = Feedback -95..95 %, 2 = Damping 0..100 %, 3 = Mix 0..100 %.
        const float freq = juce::jlimit(20.0f, 5000.0f, finiteOr(insertSlot(p, 0), 220.0f));
        const float fb   = juce::jlimit(-0.95f, 0.95f, finiteOr(insertSlot(p, 1), 70.0f) * 0.01f);
        const float damp = juce::jlimit(0.0f, 1.0f, finiteOr(insertSlot(p, 2), 30.0f) * 0.01f);
        const float mix  = juce::jlimit(0.0f, 1.0f, finiteOr(insertSlot(p, 3), 50.0f) * 0.01f);
        if (! primed)
        {
            logFreqSm.setCurrentAndTargetValue(std::log2(freq));
            fbSm.setCurrentAndTargetValue(fb);
            mixSm.setCurrentAndTargetValue(mix);
            primed = true;
        }
        logFreqSm.setTargetValue(std::log2(freq));
        fbSm.setTargetValue(fb);
        mixSm.setTargetValue(mix);

        // Mix 0 and settled: the signal is untouched and the loop is left alone (it is cleared, so it restarts clean).
        if (mix <= 0.0f && ! mixSm.isSmoothing() && mixSm.getCurrentValue() <= 0.0f)
        {
            if (wasActive)
            {
                for (auto& r : ring) std::fill(r.begin(), r.end(), 0.0f);
                for (auto& l : lp) l = 0.0f;
                wasActive = false;
            }
            return;
        }
        wasActive = true;

        // The damping cut-off runs 20 kHz -> 800 Hz exponentially; it is a block-rate coefficient.
        const float cutoff = juce::jmin(20000.0f * std::pow(800.0f / 20000.0f, damp), 0.45f * (float) sr);
        // Damping 0 means no filter in the loop at all (a fades in over the first 10 %), so the open comb is a pure delay loop.
        const float a = std::exp(-juce::MathConstants<float>::twoPi * cutoff / (float) sr) * juce::jmin(1.0f, damp * 10.0f);

        auto chans = buf.getArrayOfWritePointers();
        updateDelay(std::exp2(logFreqSm.getCurrentValue()), a);
        // Per sample: while Freq glides refresh the delay every 8 samples, read the loop, add the input, store, and mix the normalised wet.
        for (int i = 0; i < ns; ++i)
        {
            const float g = fbSm.getNextValue();
            const float m = mixSm.getNextValue();
            const float lf = logFreqSm.getNextValue();
            if (logFreqSm.isSmoothing() && (i & 7) == 0)
                updateDelay(std::exp2(lf), a);
            const float wetGain = std::sqrt(juce::jmax(0.0f, 1.0f - g * g));
            for (int ch = 0; ch < nChState; ++ch)
            {
                auto& r = ring[ch];
                const float x = sane(chans[ch][i]);
                // Lagrange read at delay `delay` samples behind the newest stored sample.
                const int n0 = writePos - delayInt;   // position of the sample `delayInt` behind the write point
                const float s0 = r[(size_t) wrap(n0 + 1)];
                const float s1 = r[(size_t) wrap(n0)];
                const float s2 = r[(size_t) wrap(n0 - 1)];
                const float s3 = r[(size_t) wrap(n0 - 2)];
                const float rd = c0 * s0 + c1 * s1 + c2 * s2 + c3 * s3;
                // One-pole damping in the loop, then the feedback sum; the clamp bounds a runaway.
                lp[ch] = flush((1.0f - a) * rd + a * lp[ch]);
                float y = x + g * lp[ch];
                y = juce::jlimit(-1.0e4f, 1.0e4f, y);
                r[(size_t) writePos] = flush(y);
                chans[ch][i] = x + m * (y * wetGain - x);
            }
            writePos = wrap(writePos + 1);
        }
        updateDelay(std::exp2(logFreqSm.getCurrentValue()), a);
    }

private:
    int wrap(int idx) const noexcept
    {
        while (idx < 0) idx += ringSize;
        while (idx >= ringSize) idx -= ringSize;
        return idx;
    }

    // Recomputes the integer and fractional read position (and the Lagrange weights) for a pitch in Hz: the loop's own delay,
    // the damping filter's group delay at that pitch, is taken off so the sounding pitch is the knob value.
    void updateDelay(float hz, float a) noexcept
    {
        const float w = juce::MathConstants<float>::twoPi * hz / (float) sr;
        const float lpDelay = (a * std::cos(w) - a * a) / (1.0f - 2.0f * a * std::cos(w) + a * a);
        float d = (float) sr / hz - lpDelay;
        d = juce::jlimit(2.0f, (float) (ringSize - 6), d);
        delayInt = (int) std::floor(d);
        const float f = d - (float) delayInt;
        c0 = -f * (f - 1.0f) * (f - 2.0f) / 6.0f;
        c1 = (f + 1.0f) * (f - 1.0f) * (f - 2.0f) * 0.5f;
        c2 = -(f + 1.0f) * f * (f - 2.0f) * 0.5f;
        c3 = (f + 1.0f) * f * (f - 1.0f) / 6.0f;
    }

    double sr = 44100.0;
    int    ringSize = 0;
    std::vector<float> ring[2];
    float  lp[2] = {};
    int    writePos = 0;
    int    delayInt = 2;
    float  c0 = 0.0f, c1 = 1.0f, c2 = 0.0f, c3 = 0.0f;
    bool   primed = false;
    bool   wasActive = true;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> logFreqSm, fbSm, mixSm;
};
}  // namespace mu_audio
