#pragma once

#include <cmath>
#include <juce_core/juce_core.h>

// The record / playback emphasis pair of a tape stage, one instance per channel. pre() is a first-order high shelf (a zero at
// 1.5 kHz, a pole at 4.2 kHz: unity at DC, about +9 dB at the top); de() is its exact inverse. Put a saturator between the two
// and small signals pass flat, while loud highs run out of headroom first and are compressed and dulled, as on tape.
// Bilinear transform with the zero and pole pre-warped, so the shelf holds its shape at 44.1, 96 and 192 kHz.
// Allocation-free: coefficients are set in prepare(), the state is one float per filter.
namespace mu_audio
{
struct TapeEmphasis
{
    static constexpr float kZeroHz = 1500.0f;
    static constexpr float kPoleHz = 4200.0f;

    void prepare(double sampleRate) noexcept
    {
        const double sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        const double wz = std::tan(juce::MathConstants<double>::pi * (double) kZeroHz / sr);
        const double wp = std::tan(juce::MathConstants<double>::pi * (double) kPoleHz  / sr);
        // H(z) = (wp / wz) * ((wz + 1) + (wz - 1) z^-1) / ((wp + 1) + (wp - 1) z^-1), unity at z = 1.
        const double nb0 = (wp / wz) * (wz + 1.0) / (wp + 1.0);
        const double nb1 = (wp / wz) * (wz - 1.0) / (wp + 1.0);
        const double na1 = (wp - 1.0) / (wp + 1.0);
        preB0 = (float) nb0;  preB1 = (float) nb1;  preA1 = (float) na1;
        // The inverse swaps numerator and denominator; its pole sits at the shelf zero, inside the unit circle, so it is stable.
        deB0 = (float) (1.0 / nb0);  deB1 = (float) (na1 / nb0);  deA1 = (float) (nb1 / nb0);
        reset();
    }

    // Transposed direct form II, one zero and one pole each.
    float pre(float x) noexcept
    {
        const float y = preB0 * x + preZ;
        preZ = preB1 * x - preA1 * y;
        return y;
    }

    float de(float x) noexcept
    {
        const float y = deB0 * x + deZ;
        deZ = deB1 * x - deA1 * y;
        return y;
    }

    void reset() noexcept { preZ = 0.0f; deZ = 0.0f; }

private:
    float preB0 = 1.0f, preB1 = 0.0f, preA1 = 0.0f, preZ = 0.0f;
    float deB0  = 1.0f, deB1  = 0.0f, deA1  = 0.0f, deZ  = 0.0f;
};
} // namespace mu_audio
