#pragma once

#include <cmath>
#include <cstdint>

namespace mu_audio::insert_support
{
// A knob value that is NaN or infinite falls back to its default.
inline float finiteOr(float v, float fallback) noexcept { return std::isfinite(v) ? v : fallback; }

// A non-finite sample is taken as silence, so one bad value cannot latch in a feedback loop or filter state.
inline float sane(float v) noexcept { return std::isfinite(v) ? v : 0.0f; }

// A state that has decayed to nothing is set to zero, so it never falls into the slow denormal range.
inline float flush(float v) noexcept { return std::abs(v) < 1.0e-20f ? 0.0f : v; }

// A per-insert noise source: xorshift32 (Marsaglia 2003). Seeded from the layer slot so two layers do not produce the same
// sequence, restarted by reset() so a reset insert equals a fresh one, and advanced once per call (never per block), so the
// output does not depend on the block size.
struct Xorshift32
{
    void seed(int slotIndex) noexcept
    {
        state = 0x9E3779B9u * (uint32_t) (slotIndex + 1) + 0x7F4A7C15u;
        if (state == 0) state = 1u;
        next();   // the first output of a weak seed is poorly mixed; discard it
    }
    uint32_t next() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    float uniform() noexcept { return (float) (next() >> 8) * (1.0f / 16777216.0f); }   // 0 <= u < 1
    float bipolar() noexcept { return uniform() * 2.0f - 1.0f; }                         // -1 <= u < 1

    uint32_t state = 1u;
};
}  // namespace mu_audio::insert_support
