#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <cmath>

// 1024-point sin lookup table with linear interpolation. Shared across all
// modulation effects (Chorus / Flanger / Phaser) so per-sample LFO evaluation
// avoids `std::sin` transcendentals. The 10-bit table gives ≈ −60 dB error
// from the true sin, well below the noise floor of any modulation effect.
//
// Usage:  const float lfoVal = SinLUT::valueForPhase(phase);
//         where phase is in [0, 1) and represents one full cycle.
//
// Wrap-around at the table boundary uses bitwise AND on the power-of-two size.
class SinLUT
{
public:
    static constexpr int kSize = 1024;
    static constexpr int kMask = kSize - 1;

    // Look up sin(2π · phase) for phase in [0, 1). Phases outside that range
    // wrap correctly via std::floor.
    static float valueForPhase(float phase) noexcept
    {
        phase -= std::floor(phase);                 // wrap to [0, 1)
        const float idxF = phase * (float)kSize;
        const int   i    = (int)idxF;
        const float frac = idxF - (float)i;
        const auto& tbl  = instance().table;
        return tbl[i & kMask] * (1.0f - frac) + tbl[(i + 1) & kMask] * frac;
    }

private:
    SinLUT() noexcept
    {
        constexpr float k = 2.0f * juce::MathConstants<float>::pi / (float)kSize;
        for (int i = 0; i < kSize; ++i)
            table[i] = std::sin(k * (float)i);
    }

    static const SinLUT& instance() noexcept
    {
        static const SinLUT s;
        return s;
    }

    std::array<float, kSize> table {};
};
