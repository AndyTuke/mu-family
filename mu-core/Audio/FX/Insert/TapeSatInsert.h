#pragma once

#include "InsertAlgorithmBase.h"
#include "Audio/AudioFilters.h"
#include "Audio/FX/Algorithms/Adaa1Tanh.h"
#include "Audio/FX/Algorithms/TapeEmphasis.h"
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <memory>

// insertAlgo = 10. Tape saturation. Signal path per channel:
//   pre-emphasis shelf -> Drive gain + small fixed offset -> tanh (ADAA) -> de-emphasis shelf     (these four at 2x the sample rate)
//   -> head bump -> DC block -> Tone -> Output                                                     (at the sample rate)
//   - Drive      0..100: 0..24 dB of gain into the saturator. With the emphasis pair around it, small signals pass flat and loud
//                highs compress and dull first, as on tape; more Drive gives denser, duller sound rather than just louder
//                (the output is trimmed by the square root of the drive gain, as SoftClip and Overdrive do).
//   - Output     -24..0 dB trim.
//   - Head Bump  0..100 %: a soft low-frequency lift around 75 Hz of up to +4 dB, the playback-head "head bump".
//   - Tone       200..20000 Hz low-pass after the saturator; fully open (19 kHz and up) bypasses it.
// The fixed offset before the curve (0.08 knee units, removed again after it) gives the mild even harmonics of tape's magnetic
// asymmetry; it is not a knob. A cheap emphasis-saturate-de-emphasis model, not a hysteresis solver.
// The saturating stage runs 2x oversampled (JUCE polyphase-IIR half-band, a few samples of delay (the impulse peaks 4 samples late), not reported or compensated:
// an insert sits per voice) because the pre-emphasis lifts highs into the curve and first-order ADAA alone left too much aliasing.
class TapeSatInsert : public InsertAlgorithmBase
{
public:
    void prepare(double sampleRate, int blockSize) override
    {
        currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
        const double osRate = 2.0 * currentSampleRate;
        maxBlock = juce::jmax(1, blockSize);
        for (auto& e : emphasis) e.prepare(osRate);
        // The Drive ramp runs once per oversampled sample, the output ramp once per sample; both keep 15 ms.
        smoothedPreGain.reset(osRate, 0.015);             smoothedPreGain.setCurrentAndTargetValue(1.0f);
        smoothedOutGain.reset(currentSampleRate, 0.015);  smoothedOutGain.setCurrentAndTargetValue(1.0f);
        // Allocated here, never on the audio thread: 2 channels, one 2x stage, the half-band IIR (the FIR types add many samples).
        oversampling = std::make_unique<juce::dsp::Oversampling<float>>(
            2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
        oversampling->initProcessing((size_t) maxBlock);
        lastBump = -1.0f;
        lastToneHz = -1.0f;
        reset();
    }
    void reset() override
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            emphasis[ch].reset();
            adaa[ch].reset(kOffset);   // silence into the shelf gives exactly the offset, so the first sample has no dip
            bumpFilter[ch].reset();
            toneFilter[ch].reset();
            dcIn [ch] = 0.0f;
            dcOut[ch] = 0.0f;
        }
        if (oversampling) oversampling->reset();
        // Snap the gain smoothers to their prepare() values so a reset slot ramps in exactly as a fresh one.
        smoothedPreGain.setCurrentAndTargetValue(1.0f);
        smoothedOutGain.setCurrentAndTargetValue(1.0f);
    }

    void process(juce::AudioBuffer<float>& buf, int ns, int nCh,
                 const VoiceParams& p, float& /*grOut*/) override
    {
        const int nChState = juce::jmin(nCh, 2);   // state is kept for two channels; extra channels pass through
        if (nChState <= 0 || ns <= 0 || oversampling == nullptr) return;
        // Slot 0 = Drive 0..100, Slot 1 = Output -24..0 dB, Slot 2 = Head Bump 0..100 %, Slot 3 = Tone 200..20000 Hz (log).
        const float drive    = insertSlot(p, 0);
        const float outputDb = insertSlot(p, 1);
        const float bump     = juce::jlimit(0.0f, 100.0f, insertSlot(p, 2)) / 100.0f;
        const float toneHz   = juce::jlimit(200.0f, 20000.0f, insertSlot(p, 3));
        const float preGain  = std::pow(10.0f, drive * 0.012f);   // 0..100 -> 1..~16x (0..24 dB)
        const float outGain  = std::pow(10.0f, outputDb / 20.0f) / std::sqrt(preGain);
        const float dcCoeff  = 1.0f - (2.0f * juce::MathConstants<float>::pi * 20.0f / (float) currentSampleRate);
        smoothedPreGain.setTargetValue(preGain);
        smoothedOutGain.setTargetValue(outGain);

        // Head bump and Tone coefficients are recomputed only when the knob moved. The bump filter always runs (at 0 % its gain is
        // 0 dB, an identity), so its state is warm and a jump of the knob from or to 0 is only a coefficient step, not a click.
        if (bump != lastBump)
        {
            for (int ch = 0; ch < 2; ++ch)
                bumpFilter[ch].setPeak(kBumpHz, kBumpQ, kBumpMaxDb * bump, (float) currentSampleRate);
            lastBump = bump;
        }
        // Tone fully open bypasses the low-pass, as the post-drive tone of the other drive inserts does.
        const bool toneOpen = toneHz >= 19000.0f;
        if (! toneOpen && toneHz != lastToneHz)
        {
            for (int ch = 0; ch < 2; ++ch)
                toneFilter[ch].prepare(toneHz, (float) currentSampleRate);
            lastToneHz = toneHz;
        }

        auto chans = buf.getArrayOfWritePointers();
        // Chunks of at most the prepared block size, so the oversampler's buffers never need to grow.
        for (int start = 0; start < ns; start += maxBlock)
        {
            const int n = juce::jmin(maxBlock, ns - start);

            // A NaN or Inf must not latch in the filter states, and a huge finite sample must not overflow the shelf state.
            for (int ch = 0; ch < nChState; ++ch)
                for (int i = start; i < start + n; ++i)
                {
                    const float in = chans[ch][i];
                    chans[ch][i] = std::isfinite(in) ? juce::jlimit(-kInputLimit, kInputLimit, in) : 0.0f;
                }

            // Phase 1: the saturating stage at twice the rate (emphasis, Drive, ADAA tanh, de-emphasis).
            juce::dsp::AudioBlock<float> io(chans, (size_t) nChState, (size_t) start, (size_t) n);
            auto up = oversampling->processSamplesUp(io);
            const size_t m = up.getNumSamples();
            // Sample-major: the Drive smoother advances once per oversampled sample and every channel uses it.
            for (size_t i = 0; i < m; ++i)
            {
                const float pg = smoothedPreGain.getNextValue();
                for (int ch = 0; ch < nChState; ++ch)
                {
                    float* d = up.getChannelPointer((size_t) ch);
                    // Highs are boosted into the curve, so they saturate first; the inverse shelf then restores the balance.
                    const float x       = emphasis[ch].pre(d[i]) * pg + kOffset;
                    const float centred = adaa[ch].process(x) - restingOffset;
                    d[i] = emphasis[ch].de(centred);
                }
            }
            oversampling->processSamplesDown(io);

            // Phase 2: head bump, DC block, Tone and Output at the sample rate.
            for (int i = start; i < start + n; ++i)
            {
                const float og = smoothedOutGain.getNextValue();
                for (int ch = 0; ch < nChState; ++ch)
                {
                    const float y = bumpFilter[ch].process(chans[ch][i]);
                    // DC block after the curve: the offset's residue and any drift would otherwise reach the tone filter.
                    float& di   = dcIn [ch];
                    float& dout = dcOut[ch];
                    const float dc = y - di + dcCoeff * dout;
                    di   = y;
                    dout = dc;
                    float t = dc;
                    if (toneOpen) toneFilter[ch].setState(dc);   // bypassed: follow the signal, so closing the tone has no step
                    else          t = toneFilter[ch].process(dc);
                    chans[ch][i] = t * og;
                }
            }
        }
    }

private:
    static constexpr float kOffset     = 0.08f;   // fixed asymmetry into the curve, in units of its knee
    static constexpr float kBumpHz     = 75.0f;
    static constexpr float kBumpQ      = 0.9f;
    static constexpr float kBumpMaxDb  = 4.0f;    // lift at Head Bump 100 %
    static constexpr float kInputLimit = 1.0e4f;  // far above any signal; keeps the shelf and curve states finite

    double    currentSampleRate = 44100.0;
    int       maxBlock = 512;
    float     lastBump = -1.0f;                   // the knob values the filter coefficients were last set for
    float     lastToneHz = -1.0f;
    const float restingOffset = std::tanh(kOffset);   // what the curve outputs for silence at this offset
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    mu_audio::TapeEmphasis emphasis[2];
    mu_audio::Adaa1Tanh    adaa[2];
    BiquadFilter           bumpFilter[2];
    OnePoleLP              toneFilter[2];
    float     dcIn [2] = {};
    float     dcOut[2] = {};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedPreGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedOutGain;
};
