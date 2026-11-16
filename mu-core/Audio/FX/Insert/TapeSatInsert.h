#pragma once

#include "InsertAlgorithmBase.h"
#include "Audio/AudioFilters.h"
#include "Audio/FX/Algorithms/Adaa1Tanh.h"
#include "Audio/FX/Algorithms/TapeEmphasis.h"
#include <cmath>

// insertAlgo = 10. Tape saturation. Signal path per channel:
//   pre-emphasis shelf -> Drive gain + small fixed offset -> tanh (ADAA) -> de-emphasis shelf -> head bump -> DC block -> Tone -> Output.
//   - Drive      0..100: 0..24 dB of gain into the saturator. With the emphasis pair around it, small signals pass flat and loud
//                highs compress and dull first, as on tape; more Drive gives denser, duller sound rather than just louder
//                (the output is trimmed by the square root of the drive gain, as SoftClip and Overdrive do).
//   - Output     -24..0 dB trim.
//   - Head Bump  0..100 %: a soft low-frequency lift around 75 Hz of up to +4 dB, the playback-head "head bump"; 0 skips it.
//   - Tone       200..20000 Hz low-pass after the saturator; fully open (19 kHz and up) bypasses it.
// The fixed offset before the curve (0.08 knee units, removed again after it) gives the mild even harmonics of tape's magnetic
// asymmetry; it is not a knob. A cheap emphasis-saturate-de-emphasis model, not a hysteresis solver.
class TapeSatInsert : public InsertAlgorithmBase
{
public:
    void prepare(double sampleRate, int) override
    {
        currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
        for (auto& e : emphasis) e.prepare(currentSampleRate);
        smoothedPreGain.reset(currentSampleRate, 0.015);  smoothedPreGain.setCurrentAndTargetValue(1.0f);
        smoothedOutGain.reset(currentSampleRate, 0.015);  smoothedOutGain.setCurrentAndTargetValue(1.0f);
        reset();
    }
    void reset() override
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            emphasis[ch].reset();
            adaa[ch].reset();
            bumpFilter[ch].reset();
            toneFilter[ch].reset();
            dcIn [ch] = 0.0f;
            dcOut[ch] = 0.0f;
        }
        // Snap the gain smoothers to their prepare() values so a reset slot ramps in exactly as a fresh one.
        smoothedPreGain.setCurrentAndTargetValue(1.0f);
        smoothedOutGain.setCurrentAndTargetValue(1.0f);
    }

    void process(juce::AudioBuffer<float>& buf, int ns, int nCh,
                 const VoiceParams& p, float& /*grOut*/) override
    {
        const int nChState = juce::jmin(nCh, 2);   // state is kept for two channels; extra channels pass through
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

        // Head bump and Tone change once per block (a coefficient step, much smaller than a gain step). A bump that is off
        // is skipped and its filter state cleared, so switching it on starts clean.
        const bool bumpOn = bump > 0.0f;
        for (int ch = 0; ch < nChState; ++ch)
        {
            if (bumpOn) bumpFilter[ch].setPeak(kBumpHz, kBumpQ, kBumpMaxDb * bump, (float) currentSampleRate);
            else        bumpFilter[ch].reset();
        }
        // Tone fully open bypasses the low-pass, as the post-drive tone of the other drive inserts does.
        const bool toneOpen = toneHz >= 19000.0f;
        if (! toneOpen)
            for (int ch = 0; ch < nChState; ++ch)
                toneFilter[ch].prepare(toneHz, (float) currentSampleRate);

        // Sample-major: each smoothed gain advances once per sample and every channel uses it.
        auto chans = buf.getArrayOfWritePointers();
        for (int i = 0; i < ns; ++i)
        {
            const float pg = smoothedPreGain.getNextValue();
            const float og = smoothedOutGain.getNextValue();
            for (int ch = 0; ch < nChState; ++ch)
            {
                float in = chans[ch][i];
                if (! std::isfinite(in)) in = 0.0f;   // a NaN or Inf must not latch in the filter states
                // Highs are boosted into the curve, so they saturate first; the inverse shelf then restores the balance.
                const float x       = emphasis[ch].pre(in) * pg + kOffset;
                const float centred = adaa[ch].process(x) - restingOffset;
                float y = emphasis[ch].de(centred);
                if (bumpOn) y = bumpFilter[ch].process(y);
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

private:
    static constexpr float kOffset    = 0.08f;   // fixed asymmetry into the curve, in units of its knee
    static constexpr float kBumpHz    = 75.0f;
    static constexpr float kBumpQ     = 0.9f;
    static constexpr float kBumpMaxDb = 4.0f;    // lift at Head Bump 100 %

    double    currentSampleRate = 44100.0;
    const float restingOffset = std::tanh(kOffset);   // what the curve outputs for silence at this offset
    mu_audio::TapeEmphasis emphasis[2];
    mu_audio::Adaa1Tanh    adaa[2];
    BiquadFilter           bumpFilter[2];
    OnePoleLP              toneFilter[2];
    float     dcIn [2] = {};
    float     dcOut[2] = {};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedPreGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedOutGain;
};
