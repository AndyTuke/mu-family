#pragma once

#include <cmath>

// Exponential decay envelope as a one-multiply recurrence: value(t) = start · e^(−t / τ),
// identical to evaluating std::exp per sample but without it. τ (the time constant, in samples)
// may change mid-decay — the curve then continues from its current level at the new rate
// instead of jumping. Allocation-free, audio-thread only.
namespace mu_core
{

class ExpDecay
{
public:
    // Time constant in samples (the level falls by 1/e every `samples`); clamped to ≥ 1.
    void setTimeConstant(float samples) noexcept
    {
        const float s = samples < 1.0f ? 1.0f : samples;
        if (s == timeConstant) return;   // setParams runs per block — skip the exp when unchanged
        timeConstant = s;
        multiplier   = std::exp(-1.0f / s);
    }

    // Restart the decay from `level`.
    void reset(float level) noexcept { value = level; }

    // This sample's level, then advance one sample.
    float next() noexcept
    {
        const float v = value;
        value *= multiplier;
        return v;
    }

private:
    float value        = 0.0f;
    float multiplier   = 1.0f;
    float timeConstant = -1.0f;
};

} // namespace mu_core
