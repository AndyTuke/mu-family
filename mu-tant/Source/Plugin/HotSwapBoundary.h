#pragma once

#include "Plugin/HotSwap.h"   // mu-core: the shared loop-wrap predicates

// mu-tant's preset hot-swap boundary. mu-tant has no master loop of its own: the transport's
// `internalBeatPos` advances freely (wrapped only at a 64-beat precision ceiling) and each
// voice's gate pattern wraps at its own `patternLengthBars`. A staged swap therefore defers to a
// *reference pattern* wrap — voice 0's pattern for a full preset, the voice's own for a per-voice
// preset (or the master loop when one is set). The predicates themselves are the shared mu-core ones.
namespace mu_tant::hotswap
{

inline bool patternWrapped(double oldPos, double newPos, double patBeats) noexcept
{
    return mu_hotswap::loopWrapped(oldPos, newPos, patBeats);
}

inline bool swapBoundaryReached(bool playing, bool wasPlaying,
                                double oldPos, double newPos, double patBeats) noexcept
{
    return mu_hotswap::boundaryReached(playing, wasPlaying, oldPos, newPos, patBeats);
}

} // namespace mu_tant::hotswap
