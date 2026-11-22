#pragma once

#include "Audio/FX/Algorithms/InsertAlgorithmBase.h"
#include "Audio/FX/Algorithms/FxUse.h"
#include "Audio/FX/Algorithms/WaveshaperCurve.h"
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <memory>
#include <vector>

namespace mu_audio
{
// insertAlgo = 24. Waveshaper: valve-style saturation, a biased triode curve that is asymmetric and so gives even harmonics (the
// distinct alternative to Overdrive's symmetric tanh and Tape Sat's emphasis pair). Signal path per channel:
//   input -> x Drive gain, divided by the Sag gain -> + Bias -> triode curve (2x oversampled) -> - resting offset
//   -> DC block -> x Output (trimmed by the square root of the drive gain).
//   - Drive 0..100: 0..30 dB of gain into the curve.
//   - Output -24..0 dB trim.
//   - Bias 0..100 %: moves the operating point along the curve (up to 0.6 curve units): more asymmetry, more even harmonics.
//   - Sag 0..100 %: loud passages pull the stage's gain down and it recovers slowly (a follower of the drive level, 10 ms attack,
//     150 ms release, stereo-linked), like a valve power supply sagging; at 0 the gain does not depend on the level.
// The curve stage runs 2x oversampled (JUCE polyphase-IIR half-band, a few samples of delay, not reported or compensated: an insert
// sits per voice), as Tape Sat does.
class WaveshaperInsert : public InsertAlgorithmBase
{
public:
    static constexpr FxUse kUse = FxUse::Insert;

    void prepare(double sampleRate, int blockSize) override
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        maxBlock = juce::jmax(1, blockSize);
        // Allocated here, never on the audio thread: the per-sample bias tables and the oversampler (2 channels, one 2x stage).
        biasBuf.assign((size_t) maxBlock, 0.0f);
        restBuf.assign((size_t) maxBlock, 0.0f);
        oversampling = std::make_unique<juce::dsp::Oversampling<float>>(
            2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
        oversampling->initProcessing((size_t) maxBlock);
        preSm.reset(sr, 0.015);
        outSm.reset(sr, 0.015);
        biasSm.reset(sr, 0.015);
        envAttack  = 1.0f - std::exp(-1.0f / (float) (0.010 * sr));
        envRelease = 1.0f - std::exp(-1.0f / (float) (0.150 * sr));
        reset();
    }

    void reset() override
    {
        env = 0.0f;
        for (int ch = 0; ch < 2; ++ch) { dcIn[ch] = 0.0f; dcOut[ch] = 0.0f; }
        if (oversampling) oversampling->reset();
        primed = false;   // the gain smoothers snap to their target on the next block, as a fresh instance does
    }

    void process(juce::AudioBuffer<float>& buf, int ns, int nCh,
                 const VoiceParams& p, float& /*grOut*/) override
    {
        const int nChState = juce::jmin(nCh, 2);   // state is kept for two channels; extra channels pass through
        if (nChState <= 0 || ns <= 0 || oversampling == nullptr) return;
        // Slot 0 = Drive 0..100, 1 = Output -24..0 dB, 2 = Bias 0..100 %, 3 = Sag 0..100 %. A NaN knob falls back to its default.
        auto finiteOr = [](float v, float fb) { return std::isfinite(v) ? v : fb; };
        const float drive    = juce::jlimit(0.0f, 100.0f, finiteOr(insertSlot(p, 0), 30.0f));
        const float outputDb = juce::jlimit(-24.0f, 0.0f, finiteOr(insertSlot(p, 1), 0.0f));
        const float bias     = juce::jlimit(0.0f, 100.0f, finiteOr(insertSlot(p, 2), 30.0f)) * 0.01f * kBiasMax;
        const float sag      = juce::jlimit(0.0f, 100.0f, finiteOr(insertSlot(p, 3), 30.0f)) * 0.01f;
        const float preGain  = std::pow(10.0f, drive * 0.01f * 30.0f / 20.0f);   // 0..100 -> 1..~31.6x (0..30 dB)
        const float outGain  = std::pow(10.0f, outputDb / 20.0f) / std::sqrt(preGain);
        if (! primed)
        {
            preSm.setCurrentAndTargetValue(preGain);
            outSm.setCurrentAndTargetValue(outGain);
            biasSm.setCurrentAndTargetValue(bias);
            primed = true;
        }
        preSm.setTargetValue(preGain);
        outSm.setTargetValue(outGain);
        biasSm.setTargetValue(bias);
        const float dcCoeff = 1.0f - (2.0f * juce::MathConstants<float>::pi * 20.0f / (float) sr);

        auto chans = buf.getArrayOfWritePointers();
        // Chunks of at most the prepared block size, so the oversampler's buffers never need to grow.
        for (int start = 0; start < ns; start += maxBlock)
        {
            const int n = juce::jmin(maxBlock, ns - start);

            // Phase 1 (base rate): guard the input, follow the drive level for Sag, apply Drive and Sag, tabulate the Bias.
            for (int i = 0; i < n; ++i)
            {
                float peak = 0.0f;
                for (int ch = 0; ch < nChState; ++ch)
                {
                    float& s = chans[ch][start + i];
                    // A NaN or Inf must not latch in the filters, and a huge finite sample must not overflow them.
                    s = std::isfinite(s) ? juce::jlimit(-kInputLimit, kInputLimit, s) : 0.0f;
                    peak = juce::jmax(peak, std::abs(s));
                }
                const float pg = preSm.getNextValue();
                const float level = juce::jmin(4.0f, peak * pg);
                env += (level - env) * (level > env ? envAttack : envRelease);
                if (env < 1.0e-20f) env = 0.0f;
                const float scale = pg / (1.0f + 0.8f * sag * env);
                for (int ch = 0; ch < nChState; ++ch)
                    chans[ch][start + i] *= scale;
                const float b = biasSm.getNextValue();
                biasBuf[(size_t) i] = b;
                restBuf[(size_t) i] = waveshaper::curve(b);
            }

            // Phase 2: the curve at twice the rate, with the bias added before it and the resting offset removed after it.
            juce::dsp::AudioBlock<float> io(chans, (size_t) nChState, (size_t) start, (size_t) n);
            auto up = oversampling->processSamplesUp(io);
            const size_t m = up.getNumSamples();
            for (int ch = 0; ch < nChState; ++ch)
            {
                float* d = up.getChannelPointer((size_t) ch);
                for (size_t j = 0; j < m; ++j)
                {
                    const size_t i = juce::jmin(j >> 1, (size_t) (n - 1));
                    d[j] = waveshaper::curve(d[j] + biasBuf[i]) - restBuf[i];
                }
            }
            oversampling->processSamplesDown(io);

            // Phase 3 (base rate): DC block and Output.
            for (int i = 0; i < n; ++i)
            {
                const float og = outSm.getNextValue();
                for (int ch = 0; ch < nChState; ++ch)
                {
                    const float y = chans[ch][start + i];
                    const float dc = y - dcIn[ch] + dcCoeff * dcOut[ch];
                    dcIn[ch] = y;
                    dcOut[ch] = std::abs(dc) < 1.0e-20f ? 0.0f : dc;
                    chans[ch][start + i] = dc * og;
                }
            }
        }
    }

private:
    static constexpr float kBiasMax    = 0.6f;    // curve units at Bias 100 %
    static constexpr float kInputLimit = 1.0e4f;  // far above any signal; keeps the curve and the filters finite

    double sr = 44100.0;
    int    maxBlock = 512;
    bool   primed = false;
    float  env = 0.0f, envAttack = 0.0f, envRelease = 0.0f;
    float  dcIn[2] = {}, dcOut[2] = {};
    std::vector<float> biasBuf, restBuf;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> preSm, outSm, biasSm;
};
}  // namespace mu_audio
