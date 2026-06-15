#pragma once

#include <array>
#include <cmath>

// Two-path IIR Hilbert transformer — produces a pair of outputs ~90° apart in
// phase across the audio band (the analytic signal's real + imaginary parts).
// Used by the X-Mod amplitude lane's SSB (single-sideband / frequency-shift) mode:
// shifting every partial by a fixed Hz needs the quadrature (sin/cos) split.
//
// Design: Olli Niemitalo's classic two-path all-pass network — two cascades of
// first-order all-pass sections whose phase responses differ by ~90°. Cheap,
// allocation-free, stable; good enough for a musical frequency shifter.
namespace mu_tant
{

class HilbertTransform
{
public:
    void reset() noexcept
    {
        for (auto& s : pathA) s.reset();
        for (auto& s : pathB) s.reset();
        delayed = 0.0f;
    }

    // Returns {real, imag}: two signals ~90° apart. `real` is the input delayed to
    // match the all-pass group delay so real/imag stay quadrature-aligned.
    struct Quad { float re, im; };
    Quad process(float x) noexcept
    {
        float a = x;
        for (auto& s : pathA) a = s.process(a);
        float b = delayed;                 // path B carries a one-sample delay vs path A
        for (auto& s : pathB) b = s.process(b);
        delayed = x;
        return { b, a };                   // {re ≈ in, im ≈ 90°-shifted}
    }

private:
    // First-order all-pass: y = c*(x + y[-1]) - x[-1], with c = coefficient.
    struct AllPass
    {
        float c = 0.0f, x1 = 0.0f, y1 = 0.0f;
        void reset() noexcept { x1 = y1 = 0.0f; }
        float process(float x) noexcept
        {
            const float y = c * (x + y1) - x1;
            x1 = x; y1 = y;
            return y;
        }
    };

    // Niemitalo coefficients (squared pole positions) for the two parallel paths.
    static constexpr std::array<float, 4> kA = { 0.6923878f, 0.9360654322959f,
                                                 0.9882295226860f, 0.9987488452737f };
    static constexpr std::array<float, 4> kB = { 0.4021921162426f, 0.8561710882420f,
                                                 0.9722909545651f, 0.9952884791278f };

    std::array<AllPass, 4> pathA = makePath(kA);
    std::array<AllPass, 4> pathB = makePath(kB);
    float delayed = 0.0f;

    static std::array<AllPass, 4> makePath(const std::array<float, 4>& coeffs) noexcept
    {
        std::array<AllPass, 4> p {};
        for (size_t i = 0; i < p.size(); ++i) p[i].c = coeffs[i];
        return p;
    }
};

} // namespace mu_tant
