#pragma once

#include "Plugin/HotSwap.h"   // mu-core: kFullPreset

namespace mu_tant
{

// mu-tant's hot-swap boundary rule, as the loop length in beats the shared swapper asks for. The
// transport beat advances freely and each voice's gate pattern wraps at its own length, so a staged
// swap defers to a pattern wrap: a per-voice preset to the voice's own (or the master loop when the
// swap timing is OnMasterLoop and a loop is set, so a swap can't hang with no master loop), a
// full preset to the master loop when one is set, else voice 0's. OnBar defers either to the bar line.
// An inactive voice never commits (-1).
inline double swapLoopBeats(int slot, int numActive, mu_hotswap::SwapTiming timing, double masterLoopBeats,
                            const double* patternBeats) noexcept
{
    // A voice's pattern length is 0 until the first block has locked it, so a swap staged before then
    // (patternBeats[] still 0) can commit only on the stop edge.
    if (slot == mu_hotswap::kFullPreset)
        return timing == mu_hotswap::SwapTiming::OnBar ? mu_hotswap::kBarBeats
                                                        : (masterLoopBeats > 0.0 ? masterLoopBeats : patternBeats[0]);
    if (slot < 0 || slot >= numActive) return -1.0;
    if (timing == mu_hotswap::SwapTiming::OnBar) return mu_hotswap::kBarBeats;
    return mu_hotswap::usesMasterLoop(timing, masterLoopBeats > 0.0) ? masterLoopBeats : patternBeats[slot];
}

} // namespace mu_tant
