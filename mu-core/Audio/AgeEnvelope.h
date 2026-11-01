#pragma once

#include <algorithm>

// A stateless attack / decay / sustain envelope read at a voice's age (samples since it started), for a gate that stays on:
// linear attack to 1, linear decay to the sustain level, then the sustain. The release is the caller's (a fade from wherever
// the voice is when it is cut). No state means no per-voice bookkeeping and no drift; it costs a couple of multiplies.
namespace mu_core
{

inline float adsAtAge(double ageSamples, float attackMs, float decayMs, float sustain, double sampleRate) noexcept
{
    const double a = (double) attackMs * 0.001 * sampleRate, d = (double) decayMs * 0.001 * sampleRate;
    if (ageSamples < a) return (float) (ageSamples / a);
    if (d <= 0.0) return sustain;
    return (float) (1.0 - (1.0 - (double) sustain) * std::min(1.0, (ageSamples - a) / d));
}

} // namespace mu_core
