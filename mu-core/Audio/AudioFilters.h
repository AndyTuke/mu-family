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

// 1-pole IIR high-pass filter. HP = input − LP(input), same coefficient formula.
struct OnePoleHP
{
    void prepare(float cutoffHz, float sampleRate) noexcept
    {
        coeff = std::exp(-juce::MathConstants<float>::twoPi * cutoffHz / sampleRate);
    }

    float process(float x) noexcept
    {
        lpState = (1.0f - coeff) * x + coeff * lpState;
        return x - lpState;
    }

    void reset() noexcept { lpState = 0.0f; }

private:
    float coeff   = 0.0f;
    float lpState = 0.0f;
};

// Allocation-free biquad filter (transposed direct form II).
// Coefficients are set via setPeak / setLowShelf / setHighShelf using Audio EQ Cookbook formulas.
// No heap allocation — safe to call on the audio thread.
struct BiquadFilter
{
    // Set all-pass coefficients (2nd order). Q controls width of phase transition.
    void setAllPass(float cutoffHz, float q, float sampleRate) noexcept
    {
        const float w0    = juce::MathConstants<float>::twoPi * cutoffHz / sampleRate;
        const float cw    = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        setCoeffs(1.0f - alpha, -2.0f * cw, 1.0f + alpha,
                  1.0f + alpha, -2.0f * cw, 1.0f - alpha);
    }

    // Set peak/bell EQ coefficients. gainDb is fixed boost; Q controls sharpness.
    void setPeak(float cutoffHz, float q, float gainDb, float sampleRate) noexcept
    {
        const float A  = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = juce::MathConstants<float>::twoPi * cutoffHz / sampleRate;
        const float cw = std::cos(w0);
        const float sw = std::sin(w0);
        const float alpha = sw / (2.0f * q);
        setCoeffs(1.0f + alpha * A,  -2.0f * cw, 1.0f - alpha * A,
                  1.0f + alpha / A,  -2.0f * cw, 1.0f - alpha / A);
    }

    // Set low-shelf coefficients. gainDb is fixed boost; Q controls shelf slope.
    void setLowShelf(float cutoffHz, float q, float gainDb, float sampleRate) noexcept
    {
        const float A  = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = juce::MathConstants<float>::twoPi * cutoffHz / sampleRate;
        const float cw = std::cos(w0);
        const float sw = std::sin(w0);
        const float alpha = sw / (2.0f * q);
        const float sa    = 2.0f * std::sqrt(A) * alpha;
        setCoeffs( A * ((A + 1.0f) - (A - 1.0f) * cw + sa),
                   A * 2.0f * ((A - 1.0f) - (A + 1.0f) * cw),
                   A * ((A + 1.0f) - (A - 1.0f) * cw - sa),
                   (A + 1.0f) + (A - 1.0f) * cw + sa,
                  -2.0f * ((A - 1.0f) + (A + 1.0f) * cw),
                   (A + 1.0f) + (A - 1.0f) * cw - sa);
    }

    // Set high-shelf coefficients. gainDb is fixed boost; Q controls shelf slope.
    void setHighShelf(float cutoffHz, float q, float gainDb, float sampleRate) noexcept
    {
        const float A  = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = juce::MathConstants<float>::twoPi * cutoffHz / sampleRate;
        const float cw = std::cos(w0);
        const float sw = std::sin(w0);
        const float alpha = sw / (2.0f * q);
        const float sa    = 2.0f * std::sqrt(A) * alpha;
        setCoeffs( A * ((A + 1.0f) + (A - 1.0f) * cw + sa),
                   A * -2.0f * ((A - 1.0f) + (A + 1.0f) * cw),
                   A * ((A + 1.0f) + (A - 1.0f) * cw - sa),
                   (A + 1.0f) - (A - 1.0f) * cw + sa,
                   2.0f * ((A - 1.0f) - (A + 1.0f) * cw),
                   (A + 1.0f) - (A - 1.0f) * cw - sa);
    }

    float process(float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void reset() noexcept { z1 = z2 = 0.0f; }

private:
    void setCoeffs(float B0, float B1, float B2, float A0, float A1, float A2) noexcept
    {
        b0 = B0 / A0;  b1 = B1 / A0;  b2 = B2 / A0;
        a1 = A1 / A0;  a2 = A2 / A0;
    }

    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;
};
