#pragma once

#include "InsertAlgorithmBase.h"
#include "Audio/AudioFilters.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>

// #423: 20-band Vocoder. Imprints the input signal's spectral envelope onto
// an internal carrier oscillator via band-by-band amplitude modulation.
//
// Signal flow (mono — input L+R are downmixed for analysis; output is the
// same vocoded mono on both channels):
//   input → 20 analysis BPs → 20 envelope followers
//   carrier (oscillator stack) → 20 synthesis BPs (same centre freqs)
//   for each band: out += synth_band * env_band
//   sum to output, replicate L+R
//
// Controls (driveChar = 12). All values stored as whole numbers in their
// respective APVTS fields — UI knobs step in integer increments:
//   driveDrive (0..100):  Waveshape index — stored as 0..3 (0=Saw, 1=Square,
//                         2=White Noise, 3=Pink Noise). UI shows the name.
//   drvBits    (1..16):   Note index +1 — stored as 1..7 (1=C, 2=D, … 7=B).
//                         The +1 offset means drvBits's natural minimum (1)
//                         maps to a valid note instead of a sentinel.
//   drvDither  (0..100):  Octave knob — stored as 1..5 (SPN octaves 2..6).
//   driveOutput (-24..0): Unison index — stored as -24..0 mapping linearly
//                         to voice counts (1, 3, 5, 7, 9, 11, 13) via
//                         `idx = (drvOut + 24) / 4`. This field's negative
//                         range avoided a clash with driveTone (which has
//                         a 20..20k clamp that would corrupt small-int
//                         storage — see #428).
//
// Carrier pitch (when waveshape is Saw/Square): SPN-anchored. #429 shifted
// the octave range down by one so Octave 1 = SPN C1 = 32.7 Hz (was C2 = 65.4 Hz).
// Octave knob 1..5 now spans SPN octaves 1..5 — same total span, just lower
// overall. White / Pink Noise carriers ignore Note / Octave / Unison entirely
// (no fundamental to detune).
//
// Click-free pitch + voice-count changes: every carrier oscillator uses a
// phase-continuous accumulator (`phase += inc; phase = fmod(phase, 1)`). When
// freq or unison count changes, only the increment value changes; the phase
// itself keeps its position. No discontinuity, no SmoothedValue needed.
class VocoderInsert : public InsertAlgorithmBase
{
public:
    void prepare(double sampleRate, int /*blockSize*/) override
    {
        currentSampleRate = sampleRate;

        // Log-spaced centre frequencies from 80 Hz → 10 kHz.
        for (int b = 0; b < kNumBands; ++b)
        {
            const float t = (float) b / (float) (kNumBands - 1);
            bandCentres[b] = kLowBandHz
                * std::pow(kHighBandHz / kLowBandHz, t);
        }

        reset();
    }

    void reset() override
    {
        for (int b = 0; b < kNumBands; ++b)
        {
            analysisBp[b].reset();
            synthBp   [b].reset();
            envelope  [b] = 0.0f;
        }
        for (auto& ph : phases) ph = 0.0f;
        for (auto& s  : pinkState) s = 0.0f;
        lastNoteIdx = -1;
        lastOctave  = -1;
        lastUnison  = -1;
    }

    void process(juce::AudioBuffer<float>& buf, int ns, int nCh,
                 const VoiceParams& p, float& /*grOut*/) override
    {
        // ── Decode controls ─────────────────────────────────────────────
        const int waveshape = juce::jlimit(0, 3, (int) std::round(p.driveDrive));
        const int noteIdx   = juce::jlimit(0, 6, (int) std::round(p.drvBits - 1.0f));
        const int octave    = juce::jlimit(1, 5, (int) std::round(p.drvDither));
        // Unison stored in driveOutput (range -24..0). Linear map to 0..6.
        const int unisonIdx = juce::jlimit(0, 6, (int) std::round((p.driveOutput + 24.0f) * 0.25f));
        const int unisonN   = kUnisonVoices[unisonIdx];

        const bool noiseCarrier = (waveshape >= 2);

        // ── Carrier frequency (pitched carriers only) ──────────────────
        static constexpr int kSemis[7] = { 0, 2, 4, 5, 7, 9, 11 };
        const float semitones  = static_cast<float>(kSemis[noteIdx] + 12 * (octave - 1));
        const float baseFreq   = kLowestFreq * std::pow(2.0f, semitones / 12.0f);
        const float detuneCents = kUnisonSpreadCents[unisonIdx];

        // ── Coefficient recompute on change (cheap path skips when nothing moved)
        const bool needRecalc = (noteIdx != lastNoteIdx)
                             || (octave  != lastOctave)
                             || (unisonIdx != lastUnison);
        if (needRecalc)
        {
            // Set up analysis + synth bandpass coeffs once per band.
            // (Centre freqs are fixed at prepare time, so this isn't really
            // about params — biquad reset on changes keeps coefs fresh.)
            const float sr = (float) currentSampleRate;
            for (int b = 0; b < kNumBands; ++b)
            {
                analysisBp[b].setPeak(bandCentres[b], kBandQ, 0.0f, sr);
                synthBp   [b].setPeak(bandCentres[b], kBandQ, 0.0f, sr);
            }
            lastNoteIdx = noteIdx;
            lastOctave  = octave;
            lastUnison  = unisonIdx;
        }

        // Envelope follower time-constants (per-sample alphas).
        const float sr    = (float) currentSampleRate;
        const float aAtk  = 1.0f - std::exp(-1.0f / (kEnvAttackMs * 0.001f * sr));
        const float aRel  = 1.0f - std::exp(-1.0f / (kEnvReleaseMs * 0.001f * sr));

        // Unison voice frequency increments + per-voice gains.
        std::array<float, kMaxUnisonVoices> voiceInc { };
        std::array<float, kMaxUnisonVoices> voiceGain { };
        float gainSum = 0.0f;
        if (! noiseCarrier)
        {
            for (int v = 0; v < unisonN; ++v)
            {
                // Symmetric placement: -1..+1 across the voice count.
                const float t = (unisonN <= 1) ? 0.0f
                                : (static_cast<float>(v) - (float) (unisonN - 1) * 0.5f)
                                        / ((float) (unisonN - 1) * 0.5f);
                const float cents = t * detuneCents;
                const float fr    = baseFreq * std::pow(2.0f, cents / 1200.0f);
                voiceInc [v] = fr / sr;
                // Outer voices drop to 0.4× the centre (per #423 spec):
                //   gain = 1 - 0.6 * (|t|)
                voiceGain[v] = 1.0f - 0.6f * std::abs(t);
                gainSum     += voiceGain[v];
            }
        }
        const float gainNorm = (gainSum > 1e-6f) ? (1.0f / gainSum) : 1.0f;

        // ── Sample loop ─────────────────────────────────────────────────
        const int nChClamped = juce::jmin(nCh, 2);
        for (int i = 0; i < ns; ++i)
        {
            // Mono down-mix of input (L+R)/2 for analysis. Stereo voicing
            // could come later; for now the vocoded output is mono-summed
            // and broadcast to both channels.
            float monoIn = 0.0f;
            for (int ch = 0; ch < nChClamped; ++ch)
                monoIn += buf.getReadPointer(ch)[i];
            monoIn *= (nChClamped > 0) ? (1.0f / (float) nChClamped) : 0.0f;

            // ── Generate carrier ────────────────────────────────────────
            float carrier = 0.0f;
            if (waveshape == 0)        // Saw
            {
                for (int v = 0; v < unisonN; ++v)
                {
                    phases[v] += voiceInc[v];
                    if (phases[v] >= 1.0f) phases[v] -= 1.0f;
                    carrier += voiceGain[v] * (2.0f * phases[v] - 1.0f);
                }
                carrier *= gainNorm;
            }
            else if (waveshape == 1)   // Square
            {
                for (int v = 0; v < unisonN; ++v)
                {
                    phases[v] += voiceInc[v];
                    if (phases[v] >= 1.0f) phases[v] -= 1.0f;
                    carrier += voiceGain[v] * (phases[v] < 0.5f ? 1.0f : -1.0f);
                }
                carrier *= gainNorm;
            }
            else if (waveshape == 2)   // White noise
            {
                carrier = rng.nextFloat() * 2.0f - 1.0f;
            }
            else                       // Pink noise (Paul Kellet)
            {
                const float w = rng.nextFloat() * 2.0f - 1.0f;
                pinkState[0] = 0.99886f * pinkState[0] + w * 0.0555179f;
                pinkState[1] = 0.99332f * pinkState[1] + w * 0.0750759f;
                pinkState[2] = 0.96900f * pinkState[2] + w * 0.1538520f;
                pinkState[3] = 0.86650f * pinkState[3] + w * 0.3104856f;
                pinkState[4] = 0.55000f * pinkState[4] + w * 0.5329522f;
                pinkState[5] = -0.7616f * pinkState[5] - w * 0.0168980f;
                carrier = pinkState[0] + pinkState[1] + pinkState[2] + pinkState[3]
                        + pinkState[4] + pinkState[5] + pinkState[6] + w * 0.5362f;
                pinkState[6] = w * 0.115926f;
                carrier *= 0.11f;   // ~unit RMS
            }

            // ── 20-band vocode ─────────────────────────────────────────
            float vocoded = 0.0f;
            for (int b = 0; b < kNumBands; ++b)
            {
                // Analysis: rectify the band-passed input, envelope-follow it.
                const float analyzed = analysisBp[b].process(monoIn);
                const float level    = std::abs(analyzed);
                const float alpha    = level > envelope[b] ? aAtk : aRel;
                envelope[b] += alpha * (level - envelope[b]);

                // Synthesis: same band on the carrier, scaled by analysis envelope.
                const float synthOut = synthBp[b].process(carrier);
                vocoded += synthOut * envelope[b];
            }

            // Broadcast vocoded output to all input channels.
            for (int ch = 0; ch < nChClamped; ++ch)
                buf.getWritePointer(ch)[i] = vocoded;
        }
    }

private:
    static constexpr int   kNumBands         = 20;
    static constexpr float kLowBandHz        = 80.0f;
    static constexpr float kHighBandHz       = 10000.0f;
    static constexpr float kBandQ            = 5.0f;
    static constexpr float kEnvAttackMs      = 5.0f;
    static constexpr float kEnvReleaseMs     = 30.0f;
    static constexpr float kLowestFreq       = 32.7f;     // SPN C1 — #429 shift
    static constexpr int   kMaxUnisonVoices  = 13;

    // Voice counts per Unison knob position (#423 spec).
    static constexpr int   kUnisonVoices      [7] = { 1, 3, 5, 7, 9, 11, 13 };
    // Detune spread D(N) in cents per Unison position.
    static constexpr float kUnisonSpreadCents [7] = { 0.0f, 8.0f, 16.0f, 24.0f, 32.0f, 40.0f, 50.0f };

    double currentSampleRate = 44100.0;

    std::array<float,       kNumBands> bandCentres { };
    std::array<BiquadFilter, kNumBands> analysisBp;
    std::array<BiquadFilter, kNumBands> synthBp;
    std::array<float,        kNumBands> envelope { };

    // Carrier state.
    std::array<float, kMaxUnisonVoices> phases { };
    float                                pinkState[7] = { };
    juce::Random                         rng;

    // Change-detection so coefficient recomputes don't run every block.
    int lastNoteIdx = -1;
    int lastOctave  = -1;
    int lastUnison  = -1;
};
