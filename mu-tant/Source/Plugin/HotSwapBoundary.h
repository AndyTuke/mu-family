#pragma once

#include <cmath>

// Pure loop-boundary predicates for mu-tant's preset hot-swap (see backlog
// #880 / #883). Extracted from PluginProcessor so the swap-defer decision can
// be unit-tested without a processor (mirrors mu-clid's HotSwapBoundary.h).
//
// mu-tant has no master loop: the transport's `internalBeatPos` advances
// freely (wrapped only at a 64-beat precision ceiling) and each voice's gate
// pattern wraps at its own `patternLengthBars`. A staged swap therefore defers
// to a *reference pattern* wrap — voice 0's pattern for a full preset, the
// voice's own pattern for a per-voice preset.
namespace mu_tant::hotswap
{

// True when a pattern of length `patBeats` wrapped between `oldPos` and
// `newPos` (both in beats; `newPos` is the raw advanced position computed
// BEFORE the transport's 64-beat ceiling wrap, so the loop-index comparison
// stays valid for pattern lengths that don't divide 64, e.g. 12 or 20 beats).
// A wrap = the integer loop index advanced across the block.
inline bool patternWrapped(double oldPos, double newPos, double patBeats) noexcept
{
    if (patBeats <= 0.0) return false;
    return std::floor(newPos / patBeats) != std::floor(oldPos / patBeats);
}

// Whether a staged swap should commit this block:
//   - playing → commit when the reference pattern wraps (musically seamless);
//   - playing→stopped edge → commit immediately (apply-on-stop: the gate is
//     held closed once stopped, so there's no audible discontinuity);
//   - stopped with no edge → never (a stopped stage is applied immediately at
//     stage time, so nothing is pending here).
inline bool swapBoundaryReached(bool playing, bool wasPlaying,
                                double oldPos, double newPos, double patBeats) noexcept
{
    if (wasPlaying && ! playing) return true;            // stop edge
    if (playing)                 return patternWrapped(oldPos, newPos, patBeats);
    return false;
}

} // namespace mu_tant::hotswap
