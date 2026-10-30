#pragma once

#include <juce_core/juce_core.h>
#include <cmath>

// Proportion-space conversions shared by every product's modulation. A skewed slider (here the filter cutoff) is
// modulated in the slider's own 0..1 proportion, so one modulation depth sweeps the same visual arc of the knob
// whatever the base value; the value is seeded with propFromCutoff() and written back with cutoffFromProp(), which
// are mutual inverses. The cutoff knob runs 20..20000 Hz with its centre at 640 Hz (skew factor ~0.2).
namespace mu_mod::skew
{

inline constexpr float kCutoffMinHz  = 20.0f;
inline constexpr float kCutoffSpanHz = 19980.0f;               // 20000 - 20
inline constexpr float kCutoffSkew   = 0.2f;
inline constexpr float kCutoffSkewInv = 1.0f / kCutoffSkew;    // exactly 5.0f in float

inline float propFromCutoff(float hz)
{
    return std::pow(juce::jlimit(0.0f, 1.0f, (hz - kCutoffMinHz) / kCutoffSpanHz), kCutoffSkew);
}

inline float cutoffFromProp(float p)
{
    return kCutoffMinHz + kCutoffSpanHz * std::pow(juce::jlimit(0.0f, 1.0f, p), kCutoffSkewInv);
}

} // namespace mu_mod::skew
