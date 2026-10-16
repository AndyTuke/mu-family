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
namespace mu_wavetable
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

    // Returns {real, imag}: two signals ~90° apart across the audio band (the upper sideband of a
    // shift is re*cos - im*sin). The one-sample delay on path A is part of the design: it puts the
    // two paths in quadrature.
    struct Quad { float re, im; };
    Quad process(float x) noexcept
    {
        float a = delayed;                 // path A carries the one-sample delay (path B takes the input directly)
        for (auto& s : pathA) a = s.process(a);
        float b = x;
        for (auto& s : pathB) b = s.process(b);
        delayed = x;
        return { b, a };                   // {re ≈ in, im ≈ 90°-shifted}
    }

private:
    // All-pass section in z^-2 (Niemitalo): y = c*(x + y[-2]) - x[-2], with c = a^2 (the squared
    // coefficient). A first-order z^-1 section with the raw coefficient gives the wrong phase
    // (image rejection falls to ~6 dB at 4 kHz); this form measures 43..60 dB from 300 Hz to 10 kHz.
    struct AllPass
    {
        float c = 0.0f, x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
        void reset() noexcept { x1 = x2 = y1 = y2 = 0.0f; }
        float process(float x) noexcept
        {
            const float y = c * (x + y2) - x2;
            x2 = x1; x1 = x;
            y2 = y1; y1 = y;
            return y;
        }
    };

    // Niemitalo coefficients a for the two parallel paths (each section uses a^2, see AllPass).
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
        for (size_t i = 0; i < p.size(); ++i) p[i].c = coeffs[i] * coeffs[i];
        return p;
    }
};

} // namespace mu_wavetable
