#pragma once

#include "InsertAlgorithmBase.h"
#include "Audio/AudioFilters.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>

// #422: Karplus-Strong physical-modelling plucked-string synthesis.
//
// Classic K-S loop: a short delay line + one-pole low-pass in the feedback
// path. Each input sample excites the delay line; the loop self-sustains
// based on the feedback gain, with the low-pass progressively damping high
// frequencies for the characteristic decaying-pluck tone.
//
// Controls (insertAlgo = 11):
//   insertDrive (0..100):   Note index — stored as 0..6 (C, D, E, F, G, A, B).
//                          UI shows the note letter.
//   insertBits    (1..16):    Octave knob — stored as 0..3 mapping to SPN octaves 1..4.
//                          #429: added Octave 0 (= SPN C1 = 32.7 Hz) below the
//                          original 1/2/3 — bottom of audible range.
//   insertDither  (0..100):   Feedback — stored as 0..100, mapped internally to
//                          loop gain 0.95..1.0. At 100% the loop self-sustains;
//                          stability is maintained by the LP filter in the
//                          feedback path (any LP cutoff < Nyquist guarantees
//                          some energy loss per cycle).
//   insertTone  (20..20k):  LP cutoff inside the feedback loop. Lower = darker
//                          / faster damping; higher = brighter / longer ring.
//                          At 20 kHz effectively bypasses the LP (no damping).
//
// Pitch math: target_freq = 32.7 Hz * 2 ^ ((semi + 12*oct) / 12)
//   where semi = {0, 2, 4, 5, 7, 9, 11} for C, D, E, F, G, A, B.
//   So Octave 0 + Note C  = 32.7 Hz  (SPN C1 — new in #429)
//      Octave 1 + Note C  = 65.4 Hz  (SPN C2)
//      Octave 3 + Note B  = 493.9 Hz (SPN B4)
//
// Click-free pitch changes: the target frequency is wrapped in a
// SmoothedValue with ~15 ms ramp time. Combined with linear-interpolated
// fractional delay reads, modulating or knob-dragging through note/octave
// values turns into a fast smooth slide rather than a phase discontinuity.
// The slide is short enough to feel like a clean note change.
class KarplusStrongInsert : public InsertAlgorithmBase
{
public:
    void prepare(double sampleRate, int /*blockSize*/) override
    {
        currentSampleRate = sampleRate;
        // #429: Delay buffer sized for the new lowest pitch (Octave 0 + Note C
        // = SPN C1 = 32.7 Hz). At 48 kHz that's ~1468 samples; allocate 2048
        // so a sample-rate change up to ~67 kHz still fits without realloc.
        const int maxDelay = static_cast<int>(sampleRate / kLowestFreq) + 16;
        for (int ch = 0; ch < 2; ++ch)
        {
            delayBuf[ch].assign((size_t) juce::jmax(maxDelay, 2048), 0.0f);
            writePos[ch] = 0;
            lpState[ch] = 0.0f;
        }
        smoothedFreq.reset(sampleRate, 0.015);   // 15 ms ramp
        smoothedFreq.setCurrentAndTargetValue(110.0f);  // arbitrary A2
    }

    void reset() override
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            std::fill(delayBuf[ch].begin(), delayBuf[ch].end(), 0.0f);
            writePos[ch] = 0;
            lpState[ch] = 0.0f;
        }
    }

    void process(juce::AudioBuffer<float>& buf, int ns, int nCh,
                 const VoiceParams& p, float& /*grOut*/) override
    {
        // Decode controls from the repurposed param fields.
        const int   noteIdx = juce::jlimit(0, 6, (int) std::round(p.insertDrive));
        // #429: octave range extended to 0..3 (was 1..3). Octave 0 = SPN 1.
        const int   octave  = juce::jlimit(0, 3, (int) std::round(p.insertBits));
        const float fbKnob  = juce::jlimit(0.0f, 1.0f, p.insertDither / 100.0f);
        // Map 0..1 onto loop gain 0.95..1.0. At max the loop is mathematically
        // self-sustaining, but the LP filter in the feedback path always
        // removes some energy per cycle so the string can ring indefinitely
        // without explosive growth. Below 0.95 the string dies in a few ms.
        const float loopGain = 0.95f + fbKnob * 0.05f;

        // Pitch: SPN-anchored at C=32.7 Hz for Octave 0 (= SPN C1). Notes
        // ascend C→B chromatically (semi offsets 0/2/4/5/7/9/11 for C..B).
        static constexpr int kSemis[7] = { 0, 2, 4, 5, 7, 9, 11 };
        const float semitones = static_cast<float>(kSemis[noteIdx] + 12 * octave);
        const float targetFreq = kLowestFreq * std::pow(2.0f, semitones / 12.0f);
        smoothedFreq.setTargetValue(targetFreq);

        // LP cutoff in the feedback path — user-controlled via insertTone
        // (range 20 Hz – 20 kHz). Lower cutoffs damp high frequencies faster
        // (darker / quicker decay); higher cutoffs let high partials through
        // (brighter / longer ring). At 20 kHz the LP is effectively bypassed,
        // letting the loop gain alone determine the decay envelope.
        const float lpCutoff = juce::jlimit(20.0f, 20000.0f, p.insertTone);
        const float lpAlpha  = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi
                                                * lpCutoff / (float) currentSampleRate);

        const int nChClamped = juce::jmin(nCh, 2);
        for (int ch = 0; ch < nChClamped; ++ch)
        {
            auto*     data    = buf.getWritePointer(ch);
            auto&     buffer  = delayBuf[ch];
            int&      wPos    = writePos[ch];
            float&    lp      = lpState[ch];
            const int bufSize = static_cast<int>(buffer.size());

            for (int i = 0; i < ns; ++i)
            {
                // Per-sample smoothed target freq → per-sample delay length.
                // Each voice reads its own next smoothed value to keep the
                // ramp sample-accurate; smoothedFreq is shared across
                // channels so we tick once and reuse for both.
                const float curFreq  = (ch == 0) ? smoothedFreq.getNextValue()
                                                 : smoothedFreq.getCurrentValue();
                const float delayLen = juce::jmax(2.0f,
                    (float) currentSampleRate / juce::jmax(20.0f, curFreq));

                // Linear-interpolated read tap (D samples behind write pos).
                const float readF = static_cast<float>(wPos) - delayLen;
                const int   r0    = ((static_cast<int>(std::floor(readF)) % bufSize) + bufSize) % bufSize;
                const int   r1    = (r0 + 1) % bufSize;
                const float frac  = readF - std::floor(readF);
                const float delayed = buffer[r0] + frac * (buffer[r1] - buffer[r0]);

                // One-pole LP on the feedback signal (the "string damping"
                // that gives K-S its decaying-pluck timbre).
                lp += lpAlpha * (delayed - lp);

                // Input excitation + filtered feedback. Output IS the loop
                // signal so the user hears the resonant body.
                const float out = data[i] + loopGain * lp;
                buffer[wPos] = out;
                data[i]      = out;
                wPos = (wPos + 1) % bufSize;
            }
        }
    }

private:
    // #429: Lowest target frequency: SPN C1 = 32.7 Hz (Octave 0 + Note C).
    static constexpr float kLowestFreq = 32.7f;

    double             currentSampleRate = 44100.0;
    std::vector<float> delayBuf[2];
    int                writePos[2] = { 0, 0 };
    float              lpState [2] = { 0.0f, 0.0f };

    // Shared across channels — stereo content has the same target pitch.
    // Sample-accurate ramp (one tick per output sample on ch 0; ch 1 reads
    // the current value).
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedFreq;
};
