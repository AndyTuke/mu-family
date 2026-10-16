#pragma once

#include <cmath>

// The beat arithmetic of the layer time model (docs/mu-tronic/design.md 2.1.5): one floor-wrap for a
// bounded beat space, and the reduction of a block-to-block beat delta that the jump check uses.
namespace mu_core
{
    // Wraps a beat into [0, wrap); wrap <= 0 means an unbounded beat space (returned unchanged).
    // On non-negative input this is exactly the resolver's old fmod lambda (fmod is exact), so no
    // render changes; a negative position (a pre-roll or count-in) wraps upward, and a tiny negative
    // value whose sum with `wrap` rounds to `wrap` itself is kept below it, never on it.
    // A NaN or infinite beat (a broken host position) has no place in the space: fmod gives NaN, and the
    // result is the largest double below `wrap` (in range, so no index overruns; the old lambda passed NaN on).
    // With wrap <= 0 a NaN or infinity is returned unchanged.
    inline double wrapBeat(double b, double wrap) noexcept
    {
        if (wrap <= 0.0) return b;
        b = std::fmod(b, wrap);
        if (b < 0.0) b += wrap;
        return b < wrap ? b : std::nextafter(wrap, 0.0);
    }

    // A beat delta reduced to the nearest multiple of `wrap` (wrap <= 0: unchanged), so the wrap of a
    // bounded host beat space is not read as a jump. Rounds to the NEAREST multiple: wrapBeat would map a
    // delta of -0.0001 to 63.9999 at wrap 64 and report a jump.
    inline double reduceBeatDelta(double d, double wrap) noexcept
    {
        if (wrap <= 0.0) return d;
        return d - wrap * std::round(d / wrap);
    }
}
