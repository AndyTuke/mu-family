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
// All eight voicings are built (`voicingFor`). Tom, Conga, Click, Wood and Pluck share the one carrier / modulator
// pair; Clank runs three pairs on the 808 metal-ratio set and is high-passed; Zap, Clank and Burst run their
// operators at 2x and decimate through a half-band FIR (the dive and the index sweep through Nyquist; the alias
// option turns the oversampling off for a lo-fi character). Wood lets the upper partial die 3x faster than the
// body, Burst adds noise to the carrier phase and re-rolls the modulator pitch every couple of milliseconds.
// Allocation-free after prepare().
namespace mu_on
{

// Per-algorithm constants: how each normalised knob maps to a musical amount for this voice.
struct FmPercVoicing
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

    // Trailing fields default so the Tom and Conga rows above keep their sound exactly.
    float skinDecayMul = 1.0f;            // The directly heard partial decays this many times faster than the body
    float noiseFm      = 0.0f;            // Noise 1.0 also phase-modulates the carrier by this many rad (noise-FM)
    float hpHz         = 0.0f;            // Output high-pass (two one-pole stages, 12 dB/oct); 0 = none
    float glitchDepth  = 0.0f;            // The modulator pitch is re-rolled by +/- this fraction ...
    float glitchMs     = 3.0f;            // ... every this many ms (sample and hold)
    float tameSlope    = 0.025f;          // Index reduction per semitone above C5 (Plaits FM engine: 0.025)
    bool  metal        = false;           // Three carrier / modulator pairs on the 808 metal-ratio set
    bool  oversample   = false;           // Run the operators at 2x and decimate through the half-band
};

class FmPercEngine
{
public:
    using Voicing = FmPercVoicing;   // defined above the class: its member initializers must be complete before the constexpr rows below use it (GCC)

    // 0 Tom: 808-style, the modulator at Ratio into the carrier and heard at 0.25 as the skin partial, a short
    // high-index knock, a subtle dive and a noise tail a little longer than the body (Baratatronix 808 tom page).
    static constexpr Voicing kTom   { 4.0f,  3.0f, 120.0f,  1.0f, 0.8f,  0.0f, 1.4f,  6.0f, 200.0f,  80.0f, 1500.0f, 90.0f,
                                      1.2f, 3.0f, 1.0f, 2000.0f, 0.25f };
    // 1 Conga: higher tunings, a shorter dive and decay, a stronger rim-ish slap at the attack, a brighter noise.
    static constexpr Voicing kConga { 3.0f,  2.0f,  40.0f,  1.5f, 0.6f,  0.0f, 0.6f,  3.0f,  60.0f,  40.0f,  600.0f, 400.0f,
                                      0.8f, 2.0f, 0.6f, 5000.0f, 0.20f };
    // 2 Zap: the laser. A large, fast downward sweep (up to 12x the Tune pitch) with a high index that sweeps down
    // with it (one oscillator with a fast falling pitch is the classic laser; MusicRadar "10 classic synth FX").
    static constexpr Voicing kZap   { 6.0f,  5.0f, 250.0f,  0.5f, 1.0f,  0.5f, 11.0f,  8.0f, 150.0f,  60.0f,  700.0f, 400.0f,
                                      1.5f, 4.0f, 0.3f, 7000.0f, 0.15f,   1.0f, 0.0f, 0.0f, 0.0f, 3.0f, 0.025f, false, true };
    // 3 Click / Rim: a sub-10 ms index edge on a noisy, ratio-rich short body: clave, rim, wood block.
    static constexpr Voicing kClick { 5.0f,  0.5f,  10.0f,  2.0f, 1.5f,  0.0f, 1.0f,  0.5f,   8.0f,   6.0f,  120.0f, 2000.0f,
                                      2.0f, 3.0f, 0.7f, 9000.0f, 0.45f,   1.0f, 0.0f, 0.0f, 0.0f, 3.0f, 0.010f, false, false };
    // 4 Clank: three modulator / carrier pairs on the 808 metal ratios (1, 1.304, 1.466, 1.787, 1.932, 2.536; Plaits
    // hi_hat.h), high-passed near 2.5 kHz; a short Decay is closed, a long one open. Ratio stretches the whole set.
    static constexpr Voicing kClank { 3.0f,  5.0f, 400.0f,  0.5f, 1.0f,  0.0f, 0.6f,  1.0f,  30.0f,  15.0f,  900.0f,  600.0f,
                                      1.5f, 6.0f, 0.5f, 9000.0f, 0.30f,   1.0f, 0.0f, 2500.0f, 0.0f, 3.0f, 0.012f, true, true };
    // 5 Wood / Mallet: a tuned bar, the heard modulator (the upper partial) dying 3x faster than the body.
    static constexpr Voicing kWood  { 2.5f,  2.0f,  60.0f,  0.6f, 1.5f,  0.0f, 0.4f,  1.0f,  12.0f,  40.0f,  900.0f,  500.0f,
                                      1.0f, 2.5f, 0.12f, 3500.0f, 0.40f,  3.0f, 0.0f, 0.0f, 0.0f, 3.0f, 0.012f, false, false };
    // 6 Pluck / Ping: a long ringing sine with the index (and so the brightness) falling away; a low Dive rises.
    static constexpr Voicing kPluck { 5.0f, 10.0f, 300.0f,  0.4f, 1.0f, -0.5f, 0.7f,  5.0f, 120.0f,  80.0f, 1500.0f,  300.0f,
                                      3.0f, 5.0f, 0.12f, 4000.0f, 0.10f,  1.0f, 0.0f, 0.0f, 0.0f, 3.0f, 0.025f, false, false };
    // 7 Burst / Glitch: high feedback, noise into the carrier phase and a modulator pitch re-rolled every 2.5 ms.
    static constexpr Voicing kBurst { 8.0f,  2.0f,  80.0f,  1.0f, 0.8f,  0.0f, 3.0f,  2.0f,  60.0f,  15.0f,  500.0f,  800.0f,
                                      4.0f, 4.0f, 0.9f, 6000.0f, 0.20f,   1.0f, 2.5f, 0.0f, 0.5f, 2.5f, 0.025f, false, true };

    // The voicing an algorithm index plays (an out-of-range index plays the Tom).
    static constexpr const Voicing& voicingFor(int algorithm) noexcept
    {
        switch (algorithm)
        {
            case 1:  return kConga;
            case 2:  return kZap;
            case 3:  return kClick;
            case 4:  return kClank;
            case 5:  return kWood;
            case 6:  return kPluck;
            case 7:  return kBurst;
            default: return kTom;
        }
    }

    void prepare(double sr, int maxBlock)
    {
        sampleRate   = sr > 0.0 ? sr : 44100.0;
        maxBlockSize = juce::jmax(1, maxBlock);
        sineTable();   // build the shared table here, off the audio thread
        holdSamples = juce::jmax(1, (int) (0.001 * sampleRate));
        rampSamples = juce::jmax(1, (int) (0.0003 * sampleRate));
        dcCoef = 1.0f - (float) (6.28318530718 * 8.0 / sampleRate);
        designHalfBand();
        reset();
    }

    // Deliberate alias: Zap, Clank and Burst run at the plain sample rate (no oversampling) for a lo-fi, aliased
    // character. Off by default; takes effect at the next trigger.
    void setDeliberateAlias(bool on) noexcept { deliberateAlias = on; }

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
        clearFilters();
        gMul = 1.0f; glitchLeft = 0;
    }

    // Add the voice's block into `buf`. An idle voice returns at once.
    void render(juce::AudioBuffer<float>& buf, int n) noexcept
    {
        if (! active && ! pending) return;
        if (active) deriveForBlock();

        const int   chs = buf.getNumChannels();
        const float invSr = 1.0f / (float) sampleRate;
        const float osF   = (float) osFactor;
        const float nyq   = 0.45f * (float) sampleRate * osF;   // the operators may run up to this at the operating rate

        for (int i = 0; i < n; ++i)
        {
            if (pending && i >= pendingOnset) startVoice();
            if (! active) continue;

            // Pitch: all operators ride the dive envelope (1 + d, d decaying to 0).
            const float pm = juce::jlimit(0.05f, 24.0f, 1.0f + diveEnv.next());
            const float fc = juce::jmin(tune * pm, nyq);

            // Glitch (Burst): the modulator pitch is re-rolled at a fixed short interval (sample and hold).
            if (glitchDepth > 0.0f && --glitchLeft <= 0)
            {
                gMul = 1.0f + glitchDepth * (rng.nextFloat() * 2.0f - 1.0f);
                glitchLeft = glitchSamples;
            }

            // Index: the body decays at FM Env, the knock is a very short extra burst at the attack.
            const float ix = indexPeak * (idxBody.next() + idxKnock.next());

            const float a  = ampEnv.next();
            const float nz = noiseEnv.next();
            const float sk = skinEnv.next();

            // Phase hold on a cold start: the phases stay at zero for the first millisecond.
            const bool advance = holdLeft <= 0;
            if (! advance) --holdLeft;

            // Per-operator phase increments this sample (cycles per sample at the operating rate).
            float cInc[3] {}, mInc[3] {};
            for (int j = 0; j < nPairs; ++j)
            {
                cInc[j] = juce::jmin(fc * cRatio[j], nyq) * invSr / osF;
                mInc[j] = juce::jmin(fc * mRatio[j] * (j == 0 ? gMul : 1.0f), 1.09f * nyq) * invSr / osF;
            }
            const float noiseFmNow = noiseFmGain * nz * kInvTwoPi;   // noise-FM depth in cycles per unit of the low-passed noise

            // The operators: once per output sample, or twice at 2x for the half-band to decimate.
            float x = 0.0f;
            for (int sub = 0; sub < osFactor; ++sub)
            {
                // Each pair: a modulator (the first with averaged two-sample self-feedback, stable at high Fdbk)
                // phase-modulating its carrier.
                float cSum = 0.0f, mSum = 0.0f;
                for (int j = 0; j < nPairs; ++j)
                {
                    const float fbTerm = j == 0 ? fbGain * 0.5f * (fb1 + fb2) * kInvTwoPi : 0.0f;
                    const float m = sine(mphase[j] + fbTerm);
                    if (j == 0) { fb2 = fb1; fb1 = m; }
                    const float c = sine(cphase[j] + ix * m * kInvTwoPi + (j == 0 ? lp * noiseFmNow : 0.0f));
                    cSum += c; mSum += m;
                    if (advance)
                    {
                        cphase[j] += cInc[j]; mphase[j] += mInc[j];
                        cphase[j] -= std::floor(cphase[j]); mphase[j] -= std::floor(mphase[j]);
                    }
                }

                // Body (carriers + the heard modulators) under the amp envelope, plus the low-passed noise layer.
                const float white = rng.nextFloat() * 2.0f - 1.0f;
                lp += lpCoef * (white - lp);
                float v = skinSame ? (cSum + skinMix * mSum) * bodyGain * a + lp * noiseGain * nz
                                   : (cSum * a + skinMix * mSum * sk) * bodyGain + lp * noiseGain * nz;

                // Fold: tanh drive, crossfading into a sine wavefold at high Fold; after the envelope, so it bites the attack.
                v = fold(v);

                // At 2x, every second sample leaves through the decimating half-band.
                if (osFactor == 2) x = halfBandPush(v, sub == 1); else x = v;
            }

            // High-pass (Clank), 8 Hz DC blocker, then the start-of-hit ramp.
            if (hpA > 0.0f)
            {
                const float h1 = hpA * (hp1y + x - hp1x);   hp1x = x;  hp1y = h1;
                const float h2 = hpA * (hp2y + h1 - hp2x);  hp2x = h1; hp2y = h2;
                x = h2;
            }
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

    // The 808 metal-oscillator ratios (Plaits hi_hat.h), taken as three modulator / carrier pairs: the carrier is the
    // first of each pair, the modulator the second.
    static constexpr float kMetalRatios[6] { 1.0f, 1.304f, 1.466f, 1.787f, 1.932f, 2.536f };

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

    // 47-tap Kaiser-window half-band low-pass (cutoff a quarter of the 2x rate, DC gain 1), built once in prepare():
    // every second tap is zero, so a decimated output costs the centre plus 12 symmetric pairs.
    static constexpr int kHbHalf = 23, kHbPairs = 12, kHistMask = 63;

    static double besselI0(double x) noexcept
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 30; ++k) { const double q = x / (2.0 * k); term *= q * q; sum += term; }
        return sum;
    }

    void designHalfBand() noexcept
    {
        const double beta = 5.65;   // about 60 dB of stop band (Kaiser 1974)
        const double centre = 0.5;
        std::array<double, kHbPairs> side {};
        double total = centre;
        for (int m = 1; m <= kHbHalf; m += 2)
        {
            const double r = (double) m / (double) kHbHalf;
            const double w = besselI0(beta * std::sqrt(1.0 - r * r)) / besselI0(beta);
            side[(size_t) (m / 2)] = std::sin(1.5707963267948966 * m) / (3.141592653589793 * m) * w;
            total += 2.0 * side[(size_t) (m / 2)];
        }
        hbCentre = (float) (centre / total);
        for (int k = 0; k < kHbPairs; ++k) hbSide[(size_t) k] = (float) (side[(size_t) k] / total);
    }

    // Push one sample of the 2x stream; with `emit` return the decimated output (the filter's centre sits 23 samples
    // back), otherwise 0.
    float halfBandPush(float x, bool emit) noexcept
    {
        hist[(size_t) (histPos & kHistMask)] = x;
        ++histPos;
        if (! emit) return 0.0f;
        const int c = histPos - 1 - kHbHalf;
        float acc = hbCentre * hist[(size_t) (c & kHistMask)];
        for (int k = 0; k < kHbPairs; ++k)
        {
            const int o = 2 * k + 1;
            acc += hbSide[(size_t) k] * (hist[(size_t) ((c - o) & kHistMask)] + hist[(size_t) ((c + o) & kHistMask)]);
        }
        return acc;
    }

    void clearFilters() noexcept
    {
        hist.fill(0.0f);
        histPos = 0;
        hp1x = hp1y = hp2x = hp2y = 0.0f;
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

        osFactor = v.oversample && ! deliberateAlias ? 2 : 1;
        const float srOp = sr * (float) osFactor;      // the rate the operators and the noise run at

        // Index is tamed above ~C5 so high tunings do not alias (Plaits FM engine: 1 - 0.025 per semitone, squared).
        const float semis  = 12.0f * std::log2(juce::jmax(tune, 1.0f) / 523.25f);
        const float taming = semis > 0.0f ? juce::jmax(0.2f, 1.0f - v.tameSlope * semis) : 1.0f;
        indexPeak = fmIndex * v.indexMax * velIndex * taming * taming;
        fbGain    = fdbk * v.fbMax;

        // Operator ratios: one pair at Ratio, or the metal set stretched about its first member by Ratio (1 = the 808 set).
        if (v.metal)
        {
            nPairs = 3;
            for (int j = 0; j < 3; ++j)
            {
                cRatio[j] = 1.0f + (kMetalRatios[2 * j]     - 1.0f) * modRatio;
                mRatio[j] = 1.0f + (kMetalRatios[2 * j + 1] - 1.0f) * modRatio;
            }
        }
        else
        {
            nPairs = 1;
            cRatio[0] = 1.0f;
            mRatio[0] = modRatio;
        }

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
        skinEnv .setTimeConstant(decayMs * 0.3333f / v.skinDecayMul * 0.001f * sr);
        noiseEnv.setTimeConstant(decayMs * 0.3333f * 1.3f * 0.001f * sr);   // the noise tail rings a little past the body
        skinSame = v.skinDecayMul == 1.0f;

        // Noise: one-pole low-pass, scaled to a unit-RMS-ish level before the knob (x0.5 headroom).
        lpCoef    = 1.0f - std::exp(-6.2831853f * v.noiseLpHz / srOp);
        const float lpRms = 0.57735f * std::sqrt(lpCoef / (2.0f - lpCoef));   // uniform white is 1/sqrt(3); the one-pole keeps sqrt(a / (2 - a))
        noiseGain   = noiseAmount * v.noiseMax / juce::jmax(lpRms, 1.0e-4f) * 0.5f;
        noiseFmGain = noiseAmount * v.noiseFm  / juce::jmax(lpRms, 1.0e-4f) * 0.5f;
        skinMix   = v.skinMix;
        bodyGain  = 1.0f / (1.0f + v.skinMix) / (float) nPairs;

        glitchDepth   = v.glitchDepth;
        glitchSamples = juce::jmax(1, (int) (v.glitchMs * 0.001f * sr));

        // Output high-pass (two identical one-pole stages).
        hpA = v.hpHz > 0.0f ? 1.0f / (1.0f + 6.2831853f * v.hpHz / sr) : 0.0f;

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
            for (int j = 0; j < 3; ++j) cphase[j] = mphase[j] = 0.0f;
            fb1 = fb2 = 0.0f; dcX = dcY = lp = 0.0f;
            clearFilters();
            gMul = 1.0f; glitchLeft = 0;
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
        skinEnv.reset(vel);
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
    bool  deliberateAlias = false;

    // The hit in progress.
    float vel = 1.0f, pendingVel = 0.0f;
    int   pendingOnset = 0;
    float cphase[3] {}, mphase[3] {}, fb1 = 0.0f, fb2 = 0.0f;
    int   holdLeft = 0, rampLeft = 0, holdSamples = 48, rampSamples = 14;
    mu_core::ExpDecay idxBody, idxKnock, diveEnv, ampEnv, skinEnv, noiseEnv;
    float gMul = 1.0f;
    int   glitchLeft = 0;

    // Derived per block from the knobs and the latched voicing.
    int   nPairs = 1, osFactor = 1;
    float cRatio[3] { 1.0f, 1.0f, 1.0f }, mRatio[3] { 1.5f, 1.0f, 1.0f };
    float indexPeak = 0.0f, fbGain = 0.0f, diveStart = 0.0f, skinMix = 0.25f, bodyGain = 0.8f;
    float lpCoef = 0.2f, noiseGain = 0.0f, noiseFmGain = 0.0f, foldDrive = 1.0f, foldMix = 0.0f, foldNorm = 1.0f;
    float glitchDepth = 0.0f, hpA = 0.0f;
    int   glitchSamples = 144;
    bool  skinSame = true;

    // Noise source and output filters.
    juce::int64  seed = mu_core::noiseSeedFor(kNoiseSeedBase, 0, 0);
    juce::Random rng { mu_core::noiseSeedFor(kNoiseSeedBase, 0, 0) };
    float lp = 0.0f, dcX = 0.0f, dcY = 0.0f, dcCoef = 0.998f;
    float hp1x = 0.0f, hp1y = 0.0f, hp2x = 0.0f, hp2y = 0.0f;

    // The decimating half-band: taps (centre + 12 symmetric pairs) and the 2x history ring.
    float hbCentre = 0.5f;
    std::array<float, kHbPairs> hbSide {};
    std::array<float, kHistMask + 1> hist {};
    int histPos = 0;
};

} // namespace mu_on
