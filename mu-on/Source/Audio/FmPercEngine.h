#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "Audio/ExpDecay.h"     // mu-core: one-multiply exponential decay
#include "Audio/NoiseSeed.h"    // mu-core: seeds for the noise generators

#include <array>
#include <cmath>

// FmPercEngine — mu-On's sixth engine: toms and techno percussion by phase-modulation FM, one voice with an
// Algorithm choice (Tom, Conga, Zap, Click, Clank, Wood, Pluck, Burst) and ten fixed knobs. Tune and Ratio are
// absolute (Hz and a ratio); the other eight are normalised 0..1 amounts that each algorithm scales through its
// own constant table (`Voicing`), so one APVTS default per knob is musical in every algorithm. The algorithm is
// latched at the trigger, so changing it never cuts a ringing tail.
//
// Voice (docs/mu-on/research-fm-drums.md): a modulator at Ratio phase-modulates a carrier (both sines, 2048-point
// table), the modulator also heard at `skinMix` as the drum-head partial; an index envelope (a decaying body plus a
// very short "knock"); a pitch-dive envelope on both operators; averaged modulator feedback; a decaying noise layer
// (low-passed, tail 1.3x the body); a tanh / sine-fold stage; an 8 Hz DC blocker. The phase is held at zero for 1 ms
// on a cold start (no pop; the Plaits bass drum holds 1.3 ms) and a 0.3 ms ramp lifts the output.
//
// Built so far: Tom (algorithm 0) and Conga (1). The other six algorithms play the Tom voicing until the next
// build step fills their rows (see `voicingFor`). Allocation-free after prepare().
namespace mu_on
{

class FmPercEngine
{
public:
    // Per-algorithm constants: how each normalised knob maps to a musical amount for this voice.
    struct Voicing
    {
        float indexMax;                       // Index 1.0 = this peak modulation index (rad)
        float idecMinMs, idecMaxMs;           // FM Env: index decay time constant (geometric map)
        float knockGain, knockMs;             // Extra attack index on top of the body, and its time constant
        float diveLo, diveHi;                 // Dive: pitch multiplier added at the hit, 1 + d (negative = rises)
        float dtMinMs, dtMaxMs;               // Dive Time (geometric map)
        float decMinMs, decMaxMs;             // Decay: perceived length (geometric map); the time constant is a third of it
        float tuneRefHz;                      // Pitch at which dive time and decay are as tabled; higher tunes get shorter
        float fbMax;                          // Fdbk 1.0 = this much modulator self-phase (rad)
        float foldMax;                        // Fold 1.0 = this much extra drive into the wavefolder
        float noiseMax, noiseLpHz;            // Noise 1.0 = this level, shaped by a one-pole low-pass
        float skinMix;                        // Level of the modulator heard directly (the drum-head partial)
    };

    // 0 Tom: 808-style, the modulator at Ratio into the carrier and heard at 0.25 as the skin partial, a short
    // high-index knock, a subtle dive and a noise tail a little longer than the body (Baratatronix 808 tom page).
    static constexpr Voicing kTom   { 4.0f,  3.0f, 120.0f,  1.0f, 0.8f,  0.0f, 1.4f,  6.0f, 200.0f,  80.0f, 1500.0f, 90.0f,
                                      1.2f, 3.0f, 1.0f, 2000.0f, 0.25f };
    // 1 Conga: higher tunings, a shorter dive and decay, a stronger rim-ish slap at the attack, a brighter noise.
    static constexpr Voicing kConga { 3.0f,  2.0f,  40.0f,  1.5f, 0.6f,  0.0f, 0.6f,  3.0f,  60.0f,  40.0f,  600.0f, 400.0f,
                                      0.8f, 2.0f, 0.6f, 5000.0f, 0.20f };

    // The voicing an algorithm index plays. Zap, Click / Rim, Clank, Wood / Mallet, Pluck / Ping and Burst / Glitch
    // are not built yet: they deliberately play the Tom voicing (a named fallback, replaced row by row).
    static constexpr const Voicing& voicingFor(int algorithm) noexcept
    {
        return algorithm == 1 ? kConga : kTom;
    }

    void prepare(double sr, int maxBlock)
    {
        sampleRate   = sr > 0.0 ? sr : 44100.0;
        maxBlockSize = juce::jmax(1, maxBlock);
        sineTable();   // build the shared table here, off the audio thread
        holdSamples = juce::jmax(1, (int) (0.001 * sampleRate));
        rampSamples = juce::jmax(1, (int) (0.0003 * sampleRate));
        dcCoef = 1.0f - (float) (6.28318530718 * 8.0 / sampleRate);
        reset();
    }

    // Per-slot noise seed (mu-core noiseSeedFor); applied at the next reset().
    void setNoiseSeed(int slot, juce::int64 salt = 0) noexcept { seed = mu_core::noiseSeedFor(kNoiseSeedBase, salt, slot); }

    // This block's knobs, in the order of kFmPercDests (MuOnModDest.h), with the Algorithm choice (not a target).
    void setParams(int algorithmIndex, float tuneHz, float ratio, float index, float indexDecay, float feedback,
                   float dive, float diveTime, float decay, float fold, float noise) noexcept
    {
        algorithm = juce::jlimit(0, 7, algorithmIndex);
        tune      = juce::jlimit(20.0f, 8000.0f, tuneHz);
        modRatio  = juce::jlimit(0.25f, 8.0f, ratio);
        fmIndex   = clamp01(index);   fmEnv  = clamp01(indexDecay); fdbk      = clamp01(feedback);
        diveAmount = clamp01(dive);   diveSpeed = clamp01(diveTime); ampDecay  = clamp01(decay);
        foldAmount = clamp01(fold);   noiseAmount = clamp01(noise);
    }

    // Fire the voice at `onset` samples into the block; velocity scales the level, Index and Dive.
    void trigger(float velocity, int onset = 0) noexcept
    {
        pendingAlgorithm = algorithm;   // latched when the hit actually starts
        pendingVel       = juce::jlimit(0.0f, 1.0f, velocity);
        pendingOnset     = juce::jmax(0, onset);
        pending          = true;
    }

    // Silence the voice at once and rewind the noise, so a render after reset() is repeatable. Allocation-free.
    void reset() noexcept
    {
        active = false; pending = false; pendingVel = 0.0f; pendingOnset = 0;
        rng.setSeed(seed);
        lp = dcX = dcY = fb1 = fb2 = 0.0f;
    }

    // Add the voice's block into `buf`. An idle voice returns at once.
    void render(juce::AudioBuffer<float>& buf, int n) noexcept
    {
        if (! active && ! pending) return;
        if (active) deriveForBlock();

        const int   chs = buf.getNumChannels();
        const float invSr = 1.0f / (float) sampleRate;
        const float nyq   = 0.45f * (float) sampleRate;

        for (int i = 0; i < n; ++i)
        {
            if (pending && i >= pendingOnset) startVoice();
            if (! active) continue;

            // Pitch: both operators ride the dive envelope (1 + d, d decaying to 0).
            const float pm = juce::jlimit(0.05f, 24.0f, 1.0f + diveEnv.next());
            const float fc = juce::jmin(tune * pm, nyq);
            const float fm = juce::jmin(fc * modRatio, 1.09f * nyq);

            // Index: the body decays at FM Env, the knock is a very short extra burst at the attack.
            const float ix = indexPeak * (idxBody.next() + idxKnock.next());

            // Modulator with averaged two-sample self-feedback (stable at high Fdbk), then the carrier it drives.
            const float m = sine(mphase + fbGain * 0.5f * (fb1 + fb2) * kInvTwoPi);
            fb2 = fb1; fb1 = m;
            const float c = sine(cphase + ix * m * kInvTwoPi);

            // Phase hold on a cold start; afterwards advance (cycles, wrapped).
            if (holdLeft > 0) --holdLeft;
            else { cphase += fc * invSr; mphase += fm * invSr; cphase -= std::floor(cphase); mphase -= std::floor(mphase); }

            const float a  = ampEnv.next();
            const float nz = noiseEnv.next();

            // Body (carrier + the skin partial) under the amp envelope, plus the low-passed noise layer.
            const float white = rng.nextFloat() * 2.0f - 1.0f;
            lp += lpCoef * (white - lp);
            float x = (c + skinMix * m) * bodyGain * a + lp * noiseGain * nz;

            // Fold: tanh drive, crossfading into a sine wavefold at high Fold; after the envelope, so it bites the attack.
            x = fold(x);

            // 8 Hz DC blocker, then the start-of-hit ramp.
            const float y = x - dcX + dcCoef * dcY;
            dcX = x; dcY = y;
            float out = y * kOutGain;
            if (rampLeft > 0) { out *= 1.0f - (float) rampLeft / (float) rampSamples; --rampLeft; }

            for (int ch = 0; ch < chs; ++ch) buf.addSample(ch, i, out);

            // Done when both the body and the noise are below -100 dB.
            if (a < kSilence && nz * noiseGain < kSilence) active = false;
        }
    }

private:
    static constexpr float kInvTwoPi = 0.15915494309f;
    static constexpr float kOutGain  = 0.9f;
    static constexpr float kSilence  = 1.0e-5f;
    static constexpr juce::int64 kNoiseSeedBase = 0x46504552;   // "FPER"

    static float clamp01(float v) noexcept { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
    static float geo(float lo, float hi, float k) noexcept { return lo * std::pow(hi / lo, k); }

    // One sine period, 2048 points plus two wrap guards, read with linear interpolation (error under -115 dB).
    struct SineTable
    {
        static constexpr int kSize = 2048;
        std::array<float, kSize + 2> v {};   // two guard points: x - floor(x) can round up to exactly 1
        SineTable() { for (int i = 0; i < kSize + 2; ++i) v[(size_t) i] = (float) std::sin(6.283185307179586 * i / kSize); }
    };
    static const SineTable& sineTable() { static const SineTable t; return t; }

    // sin(2 pi x) for x in cycles, any sign or size.
    static float sine(float x) noexcept
    {
        x -= std::floor(x);
        const float p = x * (float) SineTable::kSize;
        const int   i = (int) p;
        const float f = p - (float) i;
        const auto& t = sineTable().v;
        return t[(size_t) i] + f * (t[(size_t) i + 1] - t[(size_t) i]);
    }

    // Soft saturation, sine-folded at the top of the knob; unity-ish at Fold 0.
    float fold(float x) const noexcept
    {
        if (foldDrive <= 1.0001f) return x;
        const float s = foldDrive * x;
        const float t = std::tanh(s);
        const float f = sine(s * 0.25f);   // one fold per 4 units of drive input
        return ((1.0f - foldMix) * t + foldMix * f) * foldNorm;
    }

    // Everything the knobs and the latched voicing decide, once per block (and at the start of a hit).
    void deriveForBlock() noexcept
    {
        const Voicing& v = voicingFor(latchedAlgorithm);
        const float sr = (float) sampleRate;
        const float velIndex = 0.4f + 0.6f * vel;      // harder = brighter
        const float velDive  = 0.5f + 0.5f * vel;      // harder = deeper

        // Index is tamed above ~C5 so high tunings do not alias (Plaits FM engine: 1 - 0.025 per semitone, squared).
        const float semis  = 12.0f * std::log2(juce::jmax(tune, 1.0f) / 523.25f);
        const float taming = semis > 0.0f ? juce::jmax(0.2f, 1.0f - 0.025f * semis) : 1.0f;
        indexPeak = fmIndex * v.indexMax * velIndex * taming * taming;
        fbGain    = fdbk * v.fbMax;

        // Fixed number of cycles feels natural: the dive is quicker and the body shorter as the pitch rises.
        const float pitchRatio = tune / v.tuneRefHz;
        const float diveScale  = juce::jlimit(0.25f, 2.5f, 1.0f / std::sqrt(pitchRatio));
        const float decScale   = juce::jlimit(0.5f, 1.5f, 1.0f / std::sqrt(pitchRatio));

        idxBody .setTimeConstant(geo(v.idecMinMs, v.idecMaxMs, fmEnv) * 0.001f * sr);
        idxKnock.setTimeConstant(v.knockMs * 0.001f * sr);
        diveEnv .setTimeConstant(geo(v.dtMinMs, v.dtMaxMs, diveSpeed) * diveScale * 0.001f * sr);
        diveStart = (v.diveLo + (v.diveHi - v.diveLo) * diveAmount) * velDive;

        const float decayMs = geo(v.decMinMs, v.decMaxMs, ampDecay) * decScale;
        ampEnv  .setTimeConstant(decayMs * 0.3333f * 0.001f * sr);
        noiseEnv.setTimeConstant(decayMs * 0.3333f * 1.3f * 0.001f * sr);   // the noise tail rings a little past the body

        // Noise: one-pole low-pass, scaled to a unit-RMS-ish level before the knob (x0.5 headroom).
        lpCoef    = 1.0f - std::exp(-6.2831853f * v.noiseLpHz / sr);
        const float lpRms = 0.57735f * std::sqrt(lpCoef / (2.0f - lpCoef));   // uniform white is 1/sqrt(3); the one-pole keeps sqrt(a / (2 - a))
        noiseGain = noiseAmount * v.noiseMax / juce::jmax(lpRms, 1.0e-4f) * 0.5f;
        skinMix   = v.skinMix;
        bodyGain  = 1.0f / (1.0f + v.skinMix);

        foldDrive = 1.0f + v.foldMax * foldAmount;
        foldMix   = foldAmount * foldAmount;
        foldNorm  = 1.0f / (1.0f + 0.35f * (foldDrive - 1.0f));
    }

    void startVoice() noexcept
    {
        pending = false;
        latchedAlgorithm = pendingAlgorithm;
        vel = pendingVel;
        deriveForBlock();
        const Voicing& v = voicingFor(latchedAlgorithm);

        if (! active)
        {
            cphase = mphase = 0.0f; fb1 = fb2 = 0.0f; dcX = dcY = lp = 0.0f;
            holdLeft = holdSamples; rampLeft = rampSamples;
        }
        else
        {
            holdLeft = 0; rampLeft = 0;   // a retrigger keeps the running phases, so there is no jump
        }
        idxBody.reset(1.0f);
        idxKnock.reset(v.knockGain);
        diveEnv.reset(diveStart);
        ampEnv.reset(vel);
        noiseEnv.reset(vel);
        active = true;
    }

    double sampleRate = 44100.0;
    int    maxBlockSize = 512;
    bool   active = false, pending = false;

    // Knob values as set (normalised except tune / ratio) and the algorithm choice.
    int   algorithm = 0, latchedAlgorithm = 0, pendingAlgorithm = 0;
    float tune = 90.0f, modRatio = 1.5f, fmIndex = 0.0f, fmEnv = 0.0f, fdbk = 0.0f;
    float diveAmount = 0.0f, diveSpeed = 0.0f, ampDecay = 0.0f, foldAmount = 0.0f, noiseAmount = 0.0f;

    // The hit in progress.
    float vel = 1.0f, pendingVel = 0.0f;
    int   pendingOnset = 0;
    float cphase = 0.0f, mphase = 0.0f, fb1 = 0.0f, fb2 = 0.0f;
    int   holdLeft = 0, rampLeft = 0, holdSamples = 48, rampSamples = 14;
    mu_core::ExpDecay idxBody, idxKnock, diveEnv, ampEnv, noiseEnv;

    // Derived per block from the knobs and the latched voicing.
    float indexPeak = 0.0f, fbGain = 0.0f, diveStart = 0.0f, skinMix = 0.25f, bodyGain = 0.8f;
    float lpCoef = 0.2f, noiseGain = 0.0f, foldDrive = 1.0f, foldMix = 0.0f, foldNorm = 1.0f;

    // Noise source and output filters.
    juce::int64  seed = mu_core::noiseSeedFor(kNoiseSeedBase, 0, 0);
    juce::Random rng { mu_core::noiseSeedFor(kNoiseSeedBase, 0, 0) };
    float lp = 0.0f, dcX = 0.0f, dcY = 0.0f, dcCoef = 0.998f;
};

} // namespace mu_on
