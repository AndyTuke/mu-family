#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

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

// The same guess checked against the loop's own attacks (onset times in seconds from the loop start). Every whole-bar beat count
// in 4/4 (4, 8, 16 ...) and in 3/4 (3, 6, 12 ...) that gives a tempo in range is a candidate; each is scored by the share of
// attacks that land on its beat grid, and the best wins. A tie goes to the 4/4 reading and then the slower tempo, as the length
// alone does. So a 3.0 s loop whose hits fall on 160 BPM beats reads 160 and not 80, and a loop of whole 3/4 bars gets a tempo.
// Fewer than four attacks give no evidence and fall back to the length-only guess.
inline double estimateTempoFromOnsets(double seconds, const std::vector<double>& onsetSeconds)
{
    const double lengthOnly = estimateTempoFromLength(seconds);
    if (! (seconds > 0.0) || onsetSeconds.size() < 4) return lengthOnly;

    constexpr double kLow = 75.0, kHigh = 165.0;   // BPM
    constexpr double kTolerance = 0.035;           // seconds either side of a beat line
    constexpr double kTieMargin = 0.05;            // a later candidate must beat the best by more than this share
    double best = 0.0, bestScore = -1.0;
    for (int triple = 0; triple < 2; ++triple)
        for (int beats = triple != 0 ? 3 : 4; beats <= 64; beats *= 2)
        {
            const double bpm = (double) beats * 60.0 / seconds;
            if (! (bpm >= kLow && bpm < kHigh)) continue;
            const double beat = seconds / (double) beats;
            int on = 0;
            for (double t : onsetSeconds)
            {
                const double off = std::fmod(t, beat);
                if (std::min(off, beat - off) <= kTolerance) ++on;
            }
            const double score = (double) on / (double) onsetSeconds.size();
            if (score > bestScore + kTieMargin) { best = bpm; bestScore = score; }
        }
    return best > 0.0 ? best : lengthOnly;
}

} // namespace mu_core
