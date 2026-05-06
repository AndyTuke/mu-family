#pragma once
#include <cmath>
#include <juce_core/juce_core.h>

// Simple 1-pole IIR low-pass filter.
// y[n] = (1-a)*x[n] + a*y[n-1],  a = exp(-2π*fc/sr)
// Reusable across voice engine and any future plugin using this codebase.
struct OnePoleLP
{
    void prepare(float cutoffHz, float sampleRate) noexcept
    {
        coeff = std::exp(-juce::MathConstants<float>::twoPi * cutoffHz / sampleRate);
    }

    float process(float x) noexcept
    {
        state = (1.0f - coeff) * x + coeff * state;
        return state;
    }

    void reset() noexcept { state = 0.0f; }

private:
    float coeff = 0.0f;
    float state = 0.0f;
};
