#pragma once

#include <cmath>

// A first guess at a loop's tempo from its length alone (docs/mu-tilate/design.md, tempo): loops are cut to whole bars, so the
// length is 4, 8, 16, 32 or 64 beats. The smallest of those beat counts that gives a tempo inside the usual range wins.
// Returns 0 when the length gives no tempo in range (a one-shot, or a loop that is not whole bars).
namespace mu_core
{

inline double estimateTempoFromLength(double seconds)
{
    if (! (seconds > 0.0)) return 0.0;
    constexpr double kLow = 75.0, kHigh = 165.0;   // BPM
    for (int beats = 4; beats <= 64; beats *= 2)
    {
        const double bpm = (double) beats * 60.0 / seconds;
        if (bpm >= kLow && bpm < kHigh) return bpm;
    }
    return 0.0;
}

} // namespace mu_core
