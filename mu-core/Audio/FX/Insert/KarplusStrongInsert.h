#pragma once

#include "InsertAlgorithmBase.h"
#include "Audio/AudioFilters.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>

// Karplus-Strong physical-modelling plucked-string synthesis.
//
// Classic K-S loop: a short delay line + one-pole low-pass in the feedback
// path. Each input sample excites the delay line; the loop self-sustains
// based on the feedback gain, with the low-pass progressively damping high
// frequencies for the characteristic decaying-pluck tone.
//
// Controls (insertAlgo = 11) — the four generic insert slots (see InsertSlotConfig.h row 11):
//   Slot 0  Note     (0..11): chromatic semitone index C, C#, D, … B (kNoteNames).
//                            UI shows the note name.
//   Slot 1  Octave   (0..3):  octave offset. Octave 0 + Note C = SPN C1 = 32.7 Hz, the
//                            bottom of the range — useful for sub-bass plucks.
//   Slot 2  Feedback (0..100%): mapped via the cubic curve 1-(1-x)³ to loop gain 0..0.9999.
//                            0% = passthrough; 25% ≈ 125ms pluck; 50% ≈ 500ms;
//                            75% ≈ 4s sustain; 100% = near-infinite sustain.
//   Slot 3  LPF      (20..20k Hz): LP cutoff inside the feedback loop. Lower = darker /
//                            faster damping; higher = brighter / longer ring. At 20 kHz
//                            the LP is effectively bypassed (no damping).
//
// Pitch math: target_freq = 32.7 Hz * 2 ^ ((note + 12*oct) / 12)
//   where `note` is the chromatic 0..11 index directly (NOT a diatonic lookup).
//   So Octave 0 + Note C  = 32.7 Hz  (SPN C1)
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
        // Delay buffer sized for the new lowest pitch (Octave 0 + Note C
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
        smoothedGain.reset(sampleRate, 0.010);   // 10 ms ramp — eliminates knob crackle
        smoothedGain.setCurrentAndTargetValue(0.0f);
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
        // Slot 0 = Note 0..11 (int step), Slot 1 = Octave 0..3 (int step),
        // Slot 2 = Feedback 0..100 %, Slot 3 = LPF 20..20000 Hz (log).
        const int   noteIdx = juce::jlimit(0, 11, (int) std::round(insertSlot(p, 0)));
        const int   octave  = juce::jlimit(0, 3,  (int) std::round(insertSlot(p, 1)));
        const float fbKnob  = juce::jlimit(0.0f, 1.0f, insertSlot(p, 2) / 100.0f);
        // Cubic curve: loopGain = 1 - (1 - fbKnob)^3, capped at 0.9999.
        // Decay time scales as 1/log(loopGain), so a linear knob would compress
        // almost all audible range into the top 20 %. The cubic curve spreads it:
        //   0 %  → gain 0.000 → passthrough (no resonance)
        //  25 %  → gain 0.578 → ~125 ms decay  (short pluck)
        //  50 %  → gain 0.875 → ~500 ms decay  (clear pluck)
        //  75 %  → gain 0.984 → ~4 s decay     (long sustain)
        // 100 %  → gain 0.9999 → near-infinite sustain
        const float inv      = 1.0f - fbKnob;
        const float loopGain = juce::jmin(0.9999f, 1.0f - inv * inv * inv);
        smoothedGain.setTargetValue(loopGain);

        // Pitch: SPN-anchored at C=32.7 Hz for Octave 0 (= SPN C1). `noteIdx` is the
        // chromatic 0..11 semitone index, used directly (C=0, C#=1, … B=11).
        const float semitones = static_cast<float>(noteIdx + 12 * octave);
        const float targetFreq = kLowestFreq * std::pow(2.0f, semitones / 12.0f);
        smoothedFreq.setTargetValue(targetFreq);

        // LP cutoff in the feedback path — user-controlled via insertTone
        // (range 20 Hz – 20 kHz). Lower cutoffs damp high frequencies faster
        // (darker / quicker decay); higher cutoffs let high partials through
        // (brighter / longer ring). At 20 kHz the LP is effectively bypassed,
        // letting the loop gain alone determine the decay envelope.
        const float lpCutoff = juce::jlimit(20.0f, 20000.0f, insertSlot(p, 3));
        const float lpAlpha  = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi
                                                * lpCutoff / (float) currentSampleRate);

        // Karplus has no dedicated Output slot — pass through unity.
        const float outGain  = 1.0f;
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

                // Per-sample smoothed gain — ch0 advances the ramp, ch1 reads the
                // latest value. Eliminates crackle when the feedback knob moves.
                const float curGain = (ch == 0) ? smoothedGain.getNextValue()
                                                 : smoothedGain.getCurrentValue();

                // Input excitation + filtered feedback. Output IS the loop
                // signal so the user hears the resonant body.
                const float out = data[i] + curGain * lp;
                buffer[wPos] = out;
                data[i]      = out * outGain;
                wPos = (wPos + 1) % bufSize;
            }
        }
    }

private:
    // Lowest target frequency: SPN C1 = 32.7 Hz (Octave 0 + Note C).
    static constexpr float kLowestFreq = 32.7f;

    double             currentSampleRate = 44100.0;
    std::vector<float> delayBuf[2];
    int                writePos[2] = { 0, 0 };
    float              lpState [2] = { 0.0f, 0.0f };

    // Shared across channels — stereo content has the same target pitch/gain.
    // Sample-accurate ramp (one tick per output sample on ch 0; ch 1 reads
    // the current value — inaudible asymmetry).
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedFreq;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedGain;
};
