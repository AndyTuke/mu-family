#pragma once

#include "Plugin/HotSwap.h"   // mu-core: kFullPreset

namespace mu_tant
{

// mu-tant's hot-swap boundary rule, as the loop length in beats the shared swapper asks for. The
// transport beat advances freely and each voice's gate pattern wraps at its own length, so a staged
// swap defers to a pattern wrap: a per-voice preset to the voice's own (or the master loop when the
// Hot-swap timing is OnMasterLoop and a loop is set, so a swap can't hang with no master loop), a
// full preset to the master loop when one is set, else voice 0's. An inactive voice never commits (-1).
inline double swapLoopBeats(int slot, int numActive, bool onMasterLoop, double masterLoopBeats,
                            const double* patternBeats) noexcept
{
    if (slot == mu_hotswap::kFullPreset) return masterLoopBeats > 0.0 ? masterLoopBeats : patternBeats[0];
    if (slot < 0 || slot >= numActive)   return -1.0;
    return onMasterLoop ? masterLoopBeats : patternBeats[slot];
}

} // namespace mu_tant
