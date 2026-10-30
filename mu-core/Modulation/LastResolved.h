#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

// Hold-last-value for mu_mod::resolveLane (LaneModulation.h): a block that misses the slot's matrix lock (the
// message thread is editing its modulators) repeats the values of the last block that resolved, so no target
// snaps back to its unmodulated knob for a block. Before the first resolve with the lock held (a fresh bind, a
// prepare, a move, a layer that has just gone live) a miss keeps the unmodulated values, as a miss always did.
// Audio-thread-only, allocation-free. Call reset() on prepare, bind, a slot move and going live, so nothing held
// crosses a slot.
namespace mu_mod
{

template <std::size_t N>
struct LastResolved
{
    void reset() noexcept { have = false; }

    // Call right after resolveLane with its result: remembers `out` on a hit (or while nothing is held yet) and
    // replaces `out` with the held values on a miss. `count` <= N is the number of values in use.
    void apply(bool resolved, float* out, std::size_t count = N) noexcept
    {
        if (resolved || ! have)
        {
            std::copy(out, out + count, values.begin());
            have = have || resolved;
        }
        else
            std::copy(values.begin(), values.begin() + (std::ptrdiff_t) count, out);
    }

private:
    std::array<float, N> values {};
    bool                 have = false;
};

} // namespace mu_mod
