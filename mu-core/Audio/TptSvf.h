#pragma once

#include <algorithm>
#include <cmath>

// A per-sample, per-voice state-variable filter for a cutoff that moves with an envelope: the linear trapezoidal
// (zero-delay-feedback) form, Cytomic "SvfLinearTrapOptimised2" after Zavalishin's TPT structure. Two independent channels.
// Cheap to keep one per voice: the state is four floats and the coefficients are recomputed only when the cutoff moves
// (setCoefficients costs one tan). Stable for any cutoff in range and any resonance (the damping k never drops below 0.04).
// The band-pass tap is scaled to unity peak gain, so its level does not swing with the resonance. Never allocates.
namespace mu_core
{

class TptSvf
{
public:
    enum class Tap { lowpass, highpass, bandpass };

    void setTap(Tap t) noexcept { tap = t; }

    // cutoffHz is held between 20 Hz and 0.45 of the sample rate; resonance01 (0..1) is clamped.
    void setCoefficients(float cutoffHz, float resonance01, double sampleRate) noexcept
    {
        const double fc = std::clamp((double) cutoffHz, 20.0, 0.45 * sampleRate);
        const float g = (float) std::tan(3.14159265358979323846 * fc / sampleRate);
        k = 2.0f * (1.0f - 0.98f * std::clamp(resonance01, 0.0f, 1.0f));
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    float process(float in, int channel) noexcept
    {
        const int c = channel & 1;
        const float v3 = in - ic2[c];
        const float v1 = a1 * ic1[c] + a2 * v3;
        const float v2 = ic2[c] + a2 * ic1[c] + a3 * v3;
        ic1[c] = 2.0f * v1 - ic1[c];
        ic2[c] = 2.0f * v2 - ic2[c];
        return tap == Tap::lowpass ? v2 : (tap == Tap::highpass ? in - k * v1 - v2 : v1 * k);
    }

    void reset() noexcept { ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0.0f; }

private:
    Tap   tap = Tap::lowpass;
    float k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    float ic1[2] = { 0.0f, 0.0f }, ic2[2] = { 0.0f, 0.0f };
};

} // namespace mu_core
