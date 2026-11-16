#pragma once

#include <cmath>

namespace mu_audio
{
// A 90 degree phase network: two all-pass chains whose outputs stay 90 degrees apart (within about one degree) from roughly
// 0.002 of the sample rate up to near Nyquist. After process(), `i` and `q` form an analytic signal: `i` is the input
// (through its chain) and `q` is the same signal 90 degrees behind it, so i + j*q has positive frequencies only.
// Olli Niemitalo, "Hilbert transform with IIR all-pass filters" (2003): the 12-pole design, four first-order sections in z^-2
// per chain, y[n] = a^2 (x[n] + y[n-2]) - x[n-2]. The chain with coefficient set A carries one extra sample of delay.
struct HilbertIir
{
    void reset() noexcept
    {
        for (auto& s : sec) s = {};
        delayed = 0.0f;
    }

    void process(float x, float& i, float& q) noexcept
    {
        float b = x;   // chain B (the real part)
        float a = x;   // chain A (the imaginary part, delayed by a sample)
        for (int k = 0; k < kSections; ++k)
        {
            b = run(sec[k + kSections], kCoefB[k], b);
            a = run(sec[k], kCoefA[k], a);
        }
        i = b;
        q = delayed;
        delayed = a;
    }

private:
    static constexpr int kSections = 4;
    static constexpr float kCoefA[kSections] = { 0.6923878f * 0.6923878f, 0.9360654322959f * 0.9360654322959f,
                                                  0.9882295226860f * 0.9882295226860f, 0.9987488452737f * 0.9987488452737f };
    static constexpr float kCoefB[kSections] = { 0.4021921162426f * 0.4021921162426f, 0.8561710882420f * 0.8561710882420f,
                                                  0.9722909545651f * 0.9722909545651f, 0.9952884791278f * 0.9952884791278f };

    struct Section { float x1 = 0, x2 = 0, y1 = 0, y2 = 0; };

    // One all-pass section in z^-2 with a = the squared Niemitalo coefficient; denormals flushed.
    static float run(Section& s, float a2, float x) noexcept
    {
        float y = a2 * (x + s.y2) - s.x2;
        if (std::abs(y) < 1.0e-20f) y = 0.0f;
        s.x2 = s.x1; s.x1 = x;
        s.y2 = s.y1; s.y1 = y;
        return y;
    }

    Section sec[2 * kSections];
    float   delayed = 0.0f;
};
}  // namespace mu_audio
