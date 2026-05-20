#pragma once

#include "InsertAlgorithmBase.h"
#include "Audio/AudioFilters.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>

// 20-band Vocoder. Imprints the input signal's spectral envelope onto
// an internal carrier oscillator via band-by-band amplitude modulation.
//
// Mono mode (insertAlgo = 12): input L+R are downmixed for analysis; output
// is the same vocoded mono on both channels.
//   input → 20 analysis BPs → 20 envelope followers
//   carrier (oscillator stack) → 20 synthesis BPs (same centre freqs)
//   for each band: out += synth_band * env_band
//   sum to output, replicate L+R
//
// Stereo mode (insertAlgo = 13): separate per-channel analysis biquads and
// envelope followers, producing independent L/R vocoded output. The carrier
// and synthesis biquads are shared (same carrier signal processed per-band
// once, then scaled independently for L and R by their respective envelopes).
//   inputL → 20 analysis BPs L → 20 envelope followers L
//   inputR → 20 analysis BPs R → 20 envelope followers R
//   carrier → 20 synth BPs → outL = synthOut * envL, outR = synthOut * envR
//
// Controls (insertAlgo = 12 or 13). All values stored as whole numbers in their
// respective APVTS fields — UI knobs step in integer increments:
//   insertDrive (0..100):  Waveshape index — stored as 0..3 (0=Saw, 1=Square,
//                         2=White Noise, 3=Pink Noise). UI shows the name.
//   insertBits    (1..16):   Note index +1 — stored as 1..12 (1=C, 2=C#, … 12=B).
//                         The +1 offset means insertBits's natural minimum (1)
//                         maps to a valid note instead of a sentinel.
//   insertDither  (0..100):  Octave knob — stored as 1..5 (SPN octaves 2..6).
//   insertOutput (-24..0): Unison index — stored as -24..0 mapping linearly
//                         to voice counts (1, 3, 5, 7, 9, 11, 13) via
//                         `idx = (drvOut + 24) / 4`. This field's negative
//                         range avoided a clash with insertTone (which has
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
    explicit VocoderInsert(bool stereo = false) : stereo_(stereo) {}

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
            analysisBpL[b].reset();
            analysisBpR[b].reset();
            envelopeL  [b] = 0.0f;
            envelopeR  [b] = 0.0f;
        }
        for (auto& ph : phases) ph = 0.0f;
        for (auto& s  : pinkState) s = 0.0f;
        for (auto& g  : voiceGainSmooth) g = 0.0f;
        lastNoteIdx = -1;
        lastOctave  = -1;
        lastUnison  = -1;
    }

    void process(juce::AudioBuffer<float>& buf, int ns, int nCh,
                 const VoiceParams& p, float& /*grOut*/) override
    {
        // ── Decode controls ─────────────────────────────────────────────
        const int waveshape = juce::jlimit(0, 3, (int) std::round(p.insertDrive));
        const int noteIdx   = juce::jlimit(0, 11, (int) std::round(p.insertBits - 1.0f));
        const int octave    = juce::jlimit(1, 5, (int) std::round(p.insertDither));
        // Unison stored in insertOutput (range -24..0). Linear map to 0..6.
        const int unisonIdx = juce::jlimit(0, 6, (int) std::round((p.insertOutput + 24.0f) * 0.25f));
        const int unisonN   = kUnisonVoices[unisonIdx];

        const bool noiseCarrier = (waveshape >= 2);

        // ── Carrier frequency (pitched carriers only) ──────────────────
        const float semitones  = static_cast<float>(noteIdx + 12 * (octave - 1));
        const float baseFreq   = kLowestFreq * std::pow(2.0f, semitones / 12.0f);
        const float detuneCents = kUnisonSpreadCents[unisonIdx];

        // ── Coefficient recompute on change (cheap path skips when nothing moved)
        const bool needRecalc = (noteIdx != lastNoteIdx)
                             || (octave  != lastOctave)
                             || (unisonIdx != lastUnison);
        if (needRecalc)
        {
            const float sr = (float) currentSampleRate;
            for (int b = 0; b < kNumBands; ++b)
            {
                analysisBp [b].setPeak(bandCentres[b], kBandQ, 0.0f, sr);
                analysisBpL[b].setPeak(bandCentres[b], kBandQ, 0.0f, sr);
                analysisBpR[b].setPeak(bandCentres[b], kBandQ, 0.0f, sr);
                synthBp    [b].setPeak(bandCentres[b], kBandQ, 0.0f, sr);
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

        // per-sample exponential ramp toward each voice's target
        // gain. Newly-entered voices start at 0 (left over from when they were
        // beyond the unison count) and ramp up to their target over ~5 ms,
        // killing the audible "extra voices snap in at full volume" pop when
        // the user turns the Unison knob up live. Pitched carriers only — the
        // noise carrier path below ignores voiceGainSmooth (no per-voice mix).
        const float rampAlpha = 1.0f - std::exp(-1.0f / (kEntryRampMs * 0.001f * sr));

        // ── Sample loop ─────────────────────────────────────────────────
        const int nChClamped = juce::jmin(nCh, 2);
        for (int i = 0; i < ns; ++i)
        {
            // per-voice gain envelopes ramp toward each voice's target.
            for (int v = 0; v < kMaxUnisonVoices; ++v)
            {
                const float target = (v < unisonN) ? voiceGain[v] : 0.0f;
                voiceGainSmooth[v] += rampAlpha * (target - voiceGainSmooth[v]);
            }

            // ── Generate carrier ────────────────────────────────────────
            float carrier = 0.0f;
            if (waveshape == 0)        // Saw
            {
                for (int v = 0; v < unisonN; ++v)
                {
                    phases[v] += voiceInc[v];
                    if (phases[v] >= 1.0f) phases[v] -= 1.0f;
                    carrier += voiceGainSmooth[v] * (2.0f * phases[v] - 1.0f);
                }
                carrier *= gainNorm;
            }
            else if (waveshape == 1)   // Square
            {
                for (int v = 0; v < unisonN; ++v)
                {
                    phases[v] += voiceInc[v];
                    if (phases[v] >= 1.0f) phases[v] -= 1.0f;
                    carrier += voiceGainSmooth[v] * (phases[v] < 0.5f ? 1.0f : -1.0f);
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
            if (stereo_)
            {
                // Stereo: separate L/R analysis, shared synthesis per band.
                const float inL = (nChClamped > 0) ? buf.getReadPointer(0)[i] : 0.0f;
                const float inR = (nChClamped > 1) ? buf.getReadPointer(1)[i] : inL;
                float vocoL = 0.0f, vocoR = 0.0f;
                for (int b = 0; b < kNumBands; ++b)
                {
                    const float anaL  = analysisBpL[b].process(inL);
                    const float lvlL  = std::abs(anaL);
                    const float alpL  = lvlL > envelopeL[b] ? aAtk : aRel;
                    envelopeL[b] += alpL * (lvlL - envelopeL[b]);

                    const float anaR  = analysisBpR[b].process(inR);
                    const float lvlR  = std::abs(anaR);
                    const float alpR  = lvlR > envelopeR[b] ? aAtk : aRel;
                    envelopeR[b] += alpR * (lvlR - envelopeR[b]);

                    const float synthOut = synthBp[b].process(carrier);
                    vocoL += synthOut * envelopeL[b];
                    vocoR += synthOut * envelopeR[b];
                }
                if (nChClamped > 0) buf.getWritePointer(0)[i] = vocoL / (float)kNumBands;
                if (nChClamped > 1) buf.getWritePointer(1)[i] = vocoR / (float)kNumBands;
            }
            else
            {
                // Mono: (L+R)/2 downmix for analysis, broadcast to both channels.
                float monoIn = 0.0f;
                for (int ch = 0; ch < nChClamped; ++ch)
                    monoIn += buf.getReadPointer(ch)[i];
                monoIn *= (nChClamped > 0) ? (1.0f / (float) nChClamped) : 0.0f;

                float vocoded = 0.0f;
                for (int b = 0; b < kNumBands; ++b)
                {
                    const float analyzed = analysisBp[b].process(monoIn);
                    const float level    = std::abs(analyzed);
                    const float alpha    = level > envelope[b] ? aAtk : aRel;
                    envelope[b] += alpha * (level - envelope[b]);

                    const float synthOut = synthBp[b].process(carrier);
                    vocoded += synthOut * envelope[b];
                }
                for (int ch = 0; ch < nChClamped; ++ch)
                    buf.getWritePointer(ch)[i] = vocoded / (float)kNumBands;
            }
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
    // per-voice gain ramp time for Unison-up entries.
    static constexpr float kEntryRampMs      = 5.0f;

    // Voice counts per Unison knob position (#423 spec).
    static constexpr int   kUnisonVoices      [7] = { 1, 3, 5, 7, 9, 11, 13 };
    // Detune spread D(N) in cents per Unison position.
    static constexpr float kUnisonSpreadCents [7] = { 0.0f, 8.0f, 16.0f, 24.0f, 32.0f, 40.0f, 50.0f };

    bool stereo_ = false;

    double currentSampleRate = 44100.0;

    std::array<float,        kNumBands> bandCentres { };
    // Mono analysis path.
    std::array<BiquadFilter, kNumBands> analysisBp;
    std::array<float,        kNumBands> envelope { };
    // Stereo analysis path (stereo_ == true only).
    std::array<BiquadFilter, kNumBands> analysisBpL;
    std::array<BiquadFilter, kNumBands> analysisBpR;
    std::array<float,        kNumBands> envelopeL { };
    std::array<float,        kNumBands> envelopeR { };
    // Shared synthesis path.
    std::array<BiquadFilter, kNumBands> synthBp;

    // Carrier state.
    std::array<float, kMaxUnisonVoices> phases { };
    // smoothed per-voice gain envelope. Tracks voiceGain[v] for
    // active voices and 0 for v >= unisonN. New voices climb from 0 → target
    // over ~5 ms (kEntryRampMs) so Unison-up doesn't pop.
    std::array<float, kMaxUnisonVoices> voiceGainSmooth { };
    float                                pinkState[7] = { };
    juce::Random                         rng;

    // Change-detection so coefficient recomputes don't run every block.
    int lastNoteIdx = -1;
    int lastOctave  = -1;
    int lastUnison  = -1;
};
