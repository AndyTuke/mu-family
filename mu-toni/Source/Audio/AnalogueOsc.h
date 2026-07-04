#pragma once

#include <cmath>

// Self-contained analogue-style oscillator for μ-Toni's MVP voice.
// PolyBLEP band-limiting on saw/square/pulse; naive sine/triangle. Free-running
// phase, per-block frequency. No external content — fits "simple analogue mono
// synth" and keeps the MVP shippable without the wavetable bank / mip infra
// (which lives in mu-tant and isn't cross-product shareable yet — see buildplan).
namespace mu_toni
{

class AnalogueOsc
{
public:
    enum Shape { Sine = 0, Triangle = 1, Saw = 2, Square = 3, Pulse = 4 };
    static constexpr int kNumShapes = 5;

    void prepare(double sampleRate) noexcept { sr = sampleRate > 0 ? sampleRate : 44100.0; }
    void setFrequency(float hz) noexcept     { inc = (double) hz / sr; }
    void setShape(int s) noexcept            { shape = s < 0 ? 0 : (s >= kNumShapes ? kNumShapes - 1 : s); }
    void setPulseWidth(float w) noexcept     { pw = w < 0.05f ? 0.05f : (w > 0.95f ? 0.95f : w); }
    void resetPhase() noexcept               { phase = 0.0; }

    float render() noexcept
    {
        const double t  = phase;
        const double dt = inc;
        float value = 0.0f;

        switch (shape)
        {
            case Sine:
                value = (float) std::sin(2.0 * juce_pi * t);
                break;

            case Triangle:
                value = (float) (2.0 * std::fabs(2.0 * t - 1.0) - 1.0);   // naive (low aliasing at synth range)
                break;

            case Saw:
                value = (float) (2.0 * t - 1.0);
                value -= polyBlep(t, dt);
                break;

            case Square:
                value = t < 0.5 ? 1.0f : -1.0f;
                value += polyBlep(t, dt);
                value -= polyBlep(std::fmod(t + 0.5, 1.0), dt);
                break;

            case Pulse:
            default:
                value = t < pw ? 1.0f : -1.0f;
                value += polyBlep(t, dt);
                value -= polyBlep(std::fmod(t + (1.0 - pw), 1.0), dt);
                break;
        }

        phase += inc;
        if (phase >= 1.0) phase -= 1.0;
        return value;
    }

private:
    // PolyBLEP residual to band-limit a discontinuity at phase 0 / 1.
    static float polyBlep(double t, double dt) noexcept
    {
        if (dt <= 0.0) return 0.0f;
        if (t < dt)            { const double x = t / dt;          return (float) (x + x - x * x - 1.0); }
        if (t > 1.0 - dt)      { const double x = (t - 1.0) / dt;  return (float) (x * x + x + x + 1.0); }
        return 0.0f;
    }

    static constexpr double juce_pi = 3.14159265358979323846;

    double sr    = 44100.0;
    double phase = 0.0;
    double inc   = 0.0;
    int    shape = Saw;
    float  pw    = 0.5f;
};

} // namespace mu_toni
