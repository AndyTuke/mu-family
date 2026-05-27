#pragma once

// Pure loop-boundary predicates for the hot-swap stager. Extracted from
// HotSwapStager::checkBoundaries (#665 testability) so the swap-defer decision
// — the logic the #653 free-running fix lives in — can be unit-tested without a
// PluginProcessor. checkBoundaries() composes these with each swap's
// ready / already-fired atomic state.
namespace mu_clid::hotswap
{

// Per-rhythm staged swap. In master-loop mode (swapMode == 0) every rhythm
// defers to the master loop point; otherwise each rhythm defers to its own
// loop wrap (bit `rhythmIndex` of the mask).
inline bool perRhythmBoundaryReached(int swapMode, int rhythmIndex,
                                     bool masterLoopWrapped, int rhythmLoopWrapMask) noexcept
{
    return (swapMode == 0) ? masterLoopWrapped
                           : ((rhythmLoopWrapMask & (1 << rhythmIndex)) != 0);
}

// Full-preset swap. Defers to the master loop point when a master loop is
// defined (a preset spans every rhythm, so the master loop is the musical
// boundary). When free-running (no master loop, mstrLoop=0 default) there is no
// master wrap to wait for, so fall back to rhythm 0's loop — without this the
// swap waits forever for a boundary that never comes (#653 free-running hang).
inline bool fullPresetBoundaryReached(bool hasMasterLoop,
                                      bool masterLoopWrapped, int rhythmLoopWrapMask) noexcept
{
    return hasMasterLoop ? masterLoopWrapped
                         : ((rhythmLoopWrapMask & 1) != 0);
}

} // namespace mu_clid::hotswap
