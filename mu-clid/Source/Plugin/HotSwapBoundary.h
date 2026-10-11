#pragma once

// Pure loop-boundary predicates for the hot-swap stager. Extracted from
// HotSwapStager::checkBoundaries (for testability) so the swap-defer decision
// — the logic the free-running fix lives in — can be unit-tested without a
// PluginProcessor. checkBoundaries() composes these with each swap's
// ready / already-fired atomic state.
namespace mu_clid::hotswap
{

// True when a per-rhythm swap waits on the master loop point: the Timing setting is "On master
// loop" (swapMode 0) AND a master loop is actually set. With no master loop (free-running, the
// default) there is no master wrap to wait for, so the swap uses the rhythm's own loop instead.
inline bool perRhythmUsesMasterLoop(int swapMode, bool hasMasterLoop) noexcept
{
    return swapMode == 0 && hasMasterLoop;
}

// Per-rhythm staged swap. In master-loop mode (swapMode == 0) with a master loop set, every rhythm
// defers to the master loop point; otherwise (own-loop mode, or no master loop to wait for) each
// rhythm defers to its own loop wrap (bit `rhythmIndex` of the mask) - without that fallback the
// default settings would wait forever for a master wrap that never comes.
inline bool perRhythmBoundaryReached(int swapMode, bool hasMasterLoop, int rhythmIndex,
                                     bool masterLoopWrapped, int rhythmLoopWrapMask) noexcept
{
    return perRhythmUsesMasterLoop(swapMode, hasMasterLoop)
               ? masterLoopWrapped
               : ((rhythmLoopWrapMask & (1 << rhythmIndex)) != 0);
}

// Full-preset swap. Defers to the master loop point when a master loop is
// defined (a preset spans every rhythm, so the master loop is the musical
// boundary). When free-running (no master loop, mstrLoop=0 default) there is no
// master wrap to wait for, so fall back to rhythm 0's loop — without this the
// swap waits forever for a boundary that never comes (the free-running hang).
inline bool fullPresetBoundaryReached(bool hasMasterLoop,
                                      bool masterLoopWrapped, int rhythmLoopWrapMask) noexcept
{
    return hasMasterLoop ? masterLoopWrapped
                         : ((rhythmLoopWrapMask & 1) != 0);
}

} // namespace mu_clid::hotswap
