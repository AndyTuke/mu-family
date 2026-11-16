#pragma once

// The Waveshaper insert's transfer curve as a free function, so a test can call it alone. A simple Child-Langmuir-style soft-knee
// rational model of a biased triode (not a Koren fit): slope 1 at 0 on both sides (small signals pass at unity) while the
// second derivative differs, so even harmonics appear even with no bias.
namespace mu_audio::waveshaper
{
inline constexpr float kPositiveLimit =  1.0f;   // grid-conduction side, soft, limit +1.0
inline constexpr float kNegativeLimit = -1.5f;   // cut-off side, reached later

inline float curve(float v) noexcept
{
    return v >= 0.0f ? v / (1.0f + v)
                     : v / (1.0f - v * (1.0f / 1.5f));
}
}  // namespace mu_audio::waveshaper
