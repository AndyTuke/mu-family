#pragma once

#include <array>
#include <atomic>
#include <cstdint>

// Step parameter locks (p-locks), plugin-agnostic. A lock is an absolute value for one parameter that applies on one step of a
// step row only, like Elektron's parameter locks. This header holds the storage of one step's locks and the little the audio
// thread needs to apply them; it knows nothing about any product. The host app decides what a `key` means (mu-On: engine index
// * 256 + the destination's index in that engine's modulation table) and where the knobs are.
//
// Threads: the message thread edits a set, the audio thread reads it. Each slot is one atomic word (key and value together),
// so a read never sees a key with another lock's value. No allocation, no lock.
namespace mu_core
{

// The most locks one step can carry. Raise this one constant to allow more; storage is fixed at this size (no allocation).
inline constexpr int kMaxStepLocks = 8;

class StepLockSet
{
public:
    StepLockSet() { clear(); }

    // The slots in use, in no particular order.
    int count() const noexcept
    {
        int n = 0;
        for (const auto& s : slots) n += s.load(std::memory_order_relaxed) != kEmpty ? 1 : 0;
        return n;
    }
    bool empty() const noexcept { return count() == 0; }

    // The lock on `key`: true and its value as a proportion 0..1 in `value01`, else false.
    bool find(uint16_t key, float& value01) const noexcept
    {
        for (const auto& s : slots)
        {
            const uint32_t w = s.load(std::memory_order_relaxed);
            if (w != kEmpty && keyOf(w) == key) { value01 = valueOf(w); return true; }
        }
        return false;
    }
    bool has(uint16_t key) const noexcept { float v; return find(key, v); }

    // Lock `key` at `value01` (clamped 0..1): replaces an existing lock on the key, else takes a free slot.
    // False (nothing changed) when the key is new and all kMaxStepLocks slots are used.
    bool set(uint16_t key, float value01) noexcept
    {
        const uint32_t word = pack(key, value01);
        for (auto& s : slots)
        {
            const uint32_t w = s.load(std::memory_order_relaxed);
            if (w != kEmpty && keyOf(w) == key) { s.store(word, std::memory_order_relaxed); return true; }
        }
        for (auto& s : slots)
            if (s.load(std::memory_order_relaxed) == kEmpty) { s.store(word, std::memory_order_relaxed); return true; }
        return false;
    }

    void remove(uint16_t key) noexcept
    {
        for (auto& s : slots)
        {
            const uint32_t w = s.load(std::memory_order_relaxed);
            if (w != kEmpty && keyOf(w) == key) s.store(kEmpty, std::memory_order_relaxed);
        }
    }
    void clear() noexcept { for (auto& s : slots) s.store(kEmpty, std::memory_order_relaxed); }

    // Visit every lock: fn(key, value01).
    template <class Fn>
    void forEach(Fn&& fn) const noexcept
    {
        for (const auto& s : slots)
        {
            const uint32_t w = s.load(std::memory_order_relaxed);
            if (w != kEmpty) fn(keyOf(w), valueOf(w));
        }
    }

    // The raw word of a lock (saving) and its inverse; a value is stored in 1/65535 steps.
    static uint32_t pack(uint16_t key, float value01) noexcept
    {
        const float v = value01 < 0.0f ? 0.0f : (value01 > 1.0f ? 1.0f : value01);
        return ((uint32_t) key << 16) | (uint32_t) (v * 65535.0f + 0.5f);
    }
    static uint16_t keyOf(uint32_t word) noexcept { return (uint16_t) (word >> 16); }
    static float    valueOf(uint32_t word) noexcept { return (float) (word & 0xffffu) * (1.0f / 65535.0f); }
    static constexpr uint32_t kEmpty = 0xffffffffu;   // key 0xffff is reserved for "no lock"

private:
    std::array<std::atomic<uint32_t>, kMaxStepLocks> slots;
};

// What the audio thread hands the modulation resolve for a block: which destinations (by index in the block's table, up to 32)
// are locked, their proportions, and whether modulation still applies to them. See mu_mod::resolveLane.
struct LockSeed
{
    uint32_t mask = 0;                  // bit i: destination i is locked
    std::array<float, 32> value01 {};   // the locked proportions
    bool modulate = false;              // true: modulation moves a locked parameter from its lock; false: it is ignored for it

    // Fill from `set`: keys with high byte == `group` (mu-On: the engine) and a low byte below `count` become destinations.
    void load(const StepLockSet& set, unsigned group, int count) noexcept
    {
        mask = 0;
        set.forEach([&](uint16_t key, float v)
        {
            const int dest = key & 0xff;
            if ((unsigned) (key >> 8) == group && dest < count && dest < 32) { mask |= 1u << dest; value01[(size_t) dest] = v; }
        });
    }
};

} // namespace mu_core
