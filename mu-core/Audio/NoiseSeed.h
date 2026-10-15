#pragma once

#include <juce_core/juce_core.h>
#include <atomic>

// Seeding for the per-slot noise generators (oscillator noise sources). Live use wants every
// plugin instance and every slot to hear its own noise (two instances must not sum correlated);
// golden renders want it bit-repeatable. A per-instance salt gives the first, salt 0 the second.
namespace mu_core
{

// splitmix64 finaliser (Steele, Lea & Flood 2014): spreads a small difference such as a slot
// index across all 64 bits, which a plain XOR does not (and juce::Random is an LCG that would
// otherwise echo the low-bit similarity between neighbouring seeds).
inline juce::int64 mixNoiseSeed(juce::uint64 x) noexcept
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return (juce::int64) (x ^ (x >> 31));
}

// The seed for slot `slot` of a generator family `base`, under instance salt `salt`.
inline juce::int64 noiseSeedFor(juce::int64 base, juce::int64 salt, int slot) noexcept
{
    return mixNoiseSeed((juce::uint64) (base ^ salt ^ (juce::int64) slot));
}

// One salt per processor, taken once in its constructor (message thread). Deterministic mode
// (environment variable MU_DETERMINISTIC_NOISE set to anything but empty or "0") returns 0, so
// the golden harness and the render CLI get identical noise run to run. It is read at construction
// because the layers seed their generators while the processor is being built; set the variable
// before constructing the processor. Otherwise the salt is unique per instance: a process-wide
// counter mixed with the clock, never 0.
inline juce::int64 makeNoiseSalt()
{
    const auto flag = juce::SystemStats::getEnvironmentVariable("MU_DETERMINISTIC_NOISE", {});
    if (flag.isNotEmpty() && flag != "0")
        return 0;

    static std::atomic<juce::uint64> instanceCounter { 0 };
    const auto n     = instanceCounter.fetch_add(1, std::memory_order_relaxed);
    const auto ticks = (juce::uint64) juce::Time::getHighResolutionTicks();
    const auto salt  = mixNoiseSeed(n * 0x9E3779B97F4A7C15ull ^ ticks);
    return salt != 0 ? salt : 1;
}

} // namespace mu_core
