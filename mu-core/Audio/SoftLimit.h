#pragma once

#include <cmath>

// Soft limiter shared by the inserts: linear up to `knee`, then a tanh knee that approaches 1.0 (and saturates
// there in float), with unity slope at the knee, so the output stays inside +/-1 and a limited signal has no
// corner. Stateless and allocation-free, so it is safe anywhere on the audio thread.
namespace mu_dsp
{

constexpr float kDefaultSoftLimitKnee = 0.8f;

inline float softLimit(float x, float knee = kDefaultSoftLimitKnee) noexcept
{
    const float a = std::abs(x);
    if (a <= knee) return x;
    return std::copysign(knee + (1.0f - knee) * std::tanh((a - knee) / (1.0f - knee)), x);
}

} // namespace mu_dsp
