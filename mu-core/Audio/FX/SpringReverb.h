#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// A spring-reverb engine: a spring is dispersive (high frequencies travel faster than low ones), so an impulse returns as a
// chirp that repeats every round trip, the "boing" a plate or Schroeder reverb lacks. Two parallel spring branches, each a
// feedback loop of a delay line, a chain of first-order all-pass sections (the dispersion) and a damping low-pass; a slow
// random wobble of the delay breaks up the static comb. After Valimaki, Parker and Abel, "Parametric Spring Reverberation
// Effect", JAEC 2010, and Parker, "Efficient Dispersion Generation Structures for Spring Reverb Emulation", EURASIP 2011.
// Mono in, a stereo pair out (each branch mostly to one side with a little cross-feed). Header-only; prepare() allocates,
// process() never does. Every setter is safe to call between samples (the values are smoothed where a jump would click).
namespace mu_core
{

class SpringReverb
{
public:
    // sampleRate in Hz. Allocates the delay lines (the longest round trip plus the wobble and the interpolator).
    void prepare(double sampleRate)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        const int size = (int) (sr * kMaxRoundTripSec * kBranchRatio[1]) + 16;
        for (auto& b : branch) b.line.assign((size_t) size, 0.0f);
        lowCutCoeff = 1.0f - (float) std::exp(-2.0 * 3.14159265358979323846 * kLowCutHz / sr);
        inLpCoeff   = 1.0f - (float) std::exp(-2.0 * 3.14159265358979323846 * kInputBandHz / sr);
        smoothCoeff = 1.0f - (float) std::exp(-1.0 / (0.02 * sr));
        reset();
        setParameters(decaySec, tone, drip, tension);
        snapSmoothed();
    }

    void reset() noexcept
    {
        for (int i = 0; i < 2; ++i)
        {
            Branch& b = branch[(size_t) i];
            std::fill(b.line.begin(), b.line.end(), 0.0f);
            b.write = 0;
            b.lp = 0.0f;
            b.wobble = 0.0f; b.wobbleTarget = 0.0f; b.wobbleCount = 0;
            b.rng = 0x9e3779b9u + (uint32_t) i * 0x7f4a7c15u;
            for (auto& s : b.apX) s = 0.0f;
            for (auto& s : b.apY) s = 0.0f;
        }
        lowCutState = inLpState = 0.0f;
    }

    // decaySeconds: time for the tail to fall 60 dB (0.3..6 s). tone 0..1: loop damping, 0 dark to 1 bright. drip 0..1: how much
    // chirp (all-pass coefficient). tension 0..1: round-trip time (0 long and slack, 1 short and tight).
    void setParameters(float decaySeconds, float toneAmt, float dripAmt, float tensionAmt) noexcept
    {
        decaySec = std::clamp(std::isfinite(decaySeconds) ? decaySeconds : 2.0f, 0.3f, 6.0f);
        tone     = std::clamp(std::isfinite(toneAmt) ? toneAmt : 0.5f, 0.0f, 1.0f);
        drip     = std::clamp(std::isfinite(dripAmt) ? dripAmt : 0.5f, 0.0f, 1.0f);
        tension  = std::clamp(std::isfinite(tensionAmt) ? tensionAmt : 0.5f, 0.0f, 1.0f);
        const float roundTripSec = kMaxRoundTripSec + (kMinRoundTripSec - kMaxRoundTripSec) * tension;
        const float apCoeff = -(0.15f + 0.55f * drip);   // negative: lows are delayed more than highs, the spring's chirp
        const float lpHz = 1500.0f + 4500.0f * tone;
        const float lpCoeff = 1.0f - (float) std::exp(-2.0 * 3.14159265358979323846 * (double) lpHz / sr);
        for (int i = 0; i < 2; ++i)
        {
            Branch& b = branch[(size_t) i];
            // The dispersion's own delay counts toward the round trip: at DC each stage delays (1-a)/(1+a) samples.
            const float dcDelay = kStages * (1.0f - apCoeff) / (1.0f + apCoeff);
            const float total = roundTripSec * kBranchRatio[(size_t) i] * (float) sr;
            b.targetDelay = std::max(8.0f, total - dcDelay);
            // Loop gain so the loop falls 60 dB in decaySec: g = 10^(-3 * roundTrip / RT60).
            b.targetGain = std::pow(10.0f, -3.0f * (total / (float) sr) / decaySec);
            b.targetAp = apCoeff;
            b.targetLp = lpCoeff;
        }
    }

    // One sample in, a stereo pair out.
    void process(float in, float& outL, float& outR) noexcept
    {
        if (branch[0].line.empty()) { outL = outR = 0.0f; return; }
        // Input shaping: a gentle low cut and a band limit, since a spring tank transmits a limited mid band.
        inLpState += inLpCoeff * (in - inLpState);
        lowCutState += lowCutCoeff * (inLpState - lowCutState);
        const float x = inLpState - lowCutState;

        float y[2];
        for (int i = 0; i < 2; ++i) y[(size_t) i] = stepBranch(branch[(size_t) i], x);
        outL = kOutGain * (0.8f * y[0] + 0.2f * y[1]);
        outR = kOutGain * (0.8f * y[1] + 0.2f * y[0]);
    }

private:
    static constexpr int   kStages = 64;                 // all-pass sections per branch
    static constexpr float kMinRoundTripSec = 0.030f;
    static constexpr float kMaxRoundTripSec = 0.060f;
    static constexpr float kBranchRatio[2] = { 1.0f, 1.29f };   // unrelated round trips so the combs do not line up
    static constexpr float kLowCutHz = 120.0f;
    static constexpr float kInputBandHz = 5500.0f;
    static constexpr float kOutGain = 0.5f;
    static constexpr int   kWobbleBlock = 2048;          // samples between new random wobble targets
    static constexpr float kWobbleDepth = 1.5f;          // samples

    struct Branch
    {
        std::vector<float> line;
        int write = 0;
        float lp = 0.0f;
        std::array<float, kStages> apX {}, apY {};
        float delay = 0.0f, gain = 0.0f, ap = -0.4f, lpCoeff = 0.5f;   // smoothed values in use
        float targetDelay = 0.0f, targetGain = 0.0f, targetAp = -0.4f, targetLp = 0.5f;
        float wobble = 0.0f, wobbleTarget = 0.0f;
        int wobbleCount = 0;
        uint32_t rng = 1u;
    };

    // Moves the in-use values to their targets at once (after prepare, so the first block has no glide from zero).
    void snapSmoothed() noexcept
    {
        for (auto& b : branch) { b.delay = b.targetDelay; b.gain = b.targetGain; b.ap = b.targetAp; b.lpCoeff = b.targetLp; }
    }

    float stepBranch(Branch& b, float x) noexcept
    {
        // Parameter smoothing: one pole towards the target, so a knob turn glides instead of clicking.
        b.delay   += smoothCoeff * (b.targetDelay - b.delay);
        b.gain    += smoothCoeff * (b.targetGain - b.gain);
        b.ap      += smoothCoeff * (b.targetAp - b.ap);
        b.lpCoeff += smoothCoeff * (b.targetLp - b.lpCoeff);

        // Deterministic random wobble of the delay: a new target every block, glided to (xorshift32).
        if (--b.wobbleCount <= 0)
        {
            b.rng ^= b.rng << 13; b.rng ^= b.rng >> 17; b.rng ^= b.rng << 5;
            b.wobbleTarget = kWobbleDepth * ((float) (b.rng & 0xffff) / 32768.0f - 1.0f);
            b.wobbleCount = kWobbleBlock;
        }
        b.wobble += 0.0005f * (b.wobbleTarget - b.wobble);

        // Read the line at the wobbled delay (linear interpolation).
        const int size = (int) b.line.size();
        const float d = std::clamp(b.delay + b.wobble, 4.0f, (float) (size - 4));
        float rp = (float) b.write - d;
        if (rp < 0.0f) rp += (float) size;
        const int i0 = (int) rp;
        const float f = rp - (float) i0;
        const int i1 = i0 + 1 >= size ? 0 : i0 + 1;
        float v = b.line[(size_t) i0] + f * (b.line[(size_t) i1] - b.line[(size_t) i0]);

        // Dispersion: a chain of first-order all-passes, y[n] = a*x[n] + x[n-1] - a*y[n-1].
        for (int s = 0; s < kStages; ++s)
        {
            const float in = v;
            v = b.ap * in + b.apX[(size_t) s] - b.ap * b.apY[(size_t) s];
            b.apX[(size_t) s] = in;
            b.apY[(size_t) s] = v;
        }

        // Damping low-pass, then the loop gain; the result is both this branch's output and its feedback.
        b.lp += b.lpCoeff * (v - b.lp);
        float fb = b.gain * b.lp;
        if (std::abs(fb) < 1.0e-20f) fb = 0.0f;   // flush denormals out of the recirculating tail

        b.line[(size_t) b.write] = x + fb;
        if (++b.write >= size) b.write = 0;
        return b.lp;
    }

    double sr = 44100.0;
    float decaySec = 2.0f, tone = 0.5f, drip = 0.5f, tension = 0.5f;
    float lowCutCoeff = 0.0f, inLpCoeff = 0.0f, smoothCoeff = 0.0f;
    float lowCutState = 0.0f, inLpState = 0.0f;
    std::array<Branch, 2> branch;
};

}
