#pragma once

#include <algorithm>
#include <cmath>

// Equal-power pan law for a mono-to-stereo (or balance) gain pair: pan -1 is all left, 0 is the centre, +1 is all right,
// and gainL^2 + gainR^2 is always 2, so the centre gives 1 on both sides (a mono source keeps its level there).
namespace mu_core
{

inline void equalPowerPan(float pan, float& gainL, float& gainR) noexcept
{
    constexpr float kSqrt2 = 1.41421356f;
    constexpr float kPi = 3.14159265f;
    const float angle = (std::clamp(pan, -1.0f, 1.0f) + 1.0f) * 0.25f * kPi;   // 0 .. pi/2
    gainL = std::cos(angle) * kSqrt2;
    gainR = std::sin(angle) * kSqrt2;
}

} // namespace mu_core
