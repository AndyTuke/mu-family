#pragma once

#include <cmath>

// First-order anti-derivative anti-aliased (ADAA) tanh, one instance per channel. Instead of tanh(x[n]) it returns the average of
// tanh over the step from x[n-1] to x[n], computed exactly from the antiderivative log(cosh x), which cuts the aliasing a plain
// waveshaper folds back from its harmonics (Parker et al. 2016, "Reducing the aliasing of nonlinear waveshaping using
// continuous-time convolution"). Shared by the drive inserts (Overdrive, Tape Sat); the one-sample step average also gives a gentle
// high-frequency droop (about -2 dB at 10 kHz at 48 kHz). Allocation-free, safe on the audio thread.
namespace mu_audio
{
struct Adaa1Tanh
{
    // log(cosh x), written in the large-|x| form that cannot overflow.
    static float antiderivative(float x) noexcept
    {
        return std::abs(x) > 12.0f ? std::abs(x) - 0.6931472f : std::log(std::cosh(x));
    }

    float process(float x) noexcept
    {
        const float dx = x - xPrev;
        // Both points past the knee on the same side: tanh is flat at exactly +-1, and the large-|x| ADAA form would divide a
        // float rounding error by a tiny dx (noise on a loud low note's flat top).
        const float sat = (x > 12.0f && xPrev > 12.0f)    ?  1.0f
                        : (x < -12.0f && xPrev < -12.0f)  ? -1.0f
                        : std::abs(dx) < 1e-4f            ? std::tanh(0.5f * (x + xPrev))
                                                          : (antiderivative(x) - antiderivative(xPrev)) / dx;
        xPrev = x;
        return sat;
    }

    // The previous input a fresh stream is taken to start from; a caller whose silence is not 0 (an offset before the curve) passes it,
    // so the first sample has no step from 0.
    void reset(float x0 = 0.0f) noexcept { xPrev = x0; }

private:
    float xPrev = 0.0f;
};
} // namespace mu_audio
