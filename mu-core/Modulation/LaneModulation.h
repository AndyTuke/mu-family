#pragma once

#include "Audio/SpinLock.h"   // mu-core: spin lock helpers
#include "Sequencer/Layer.h"
#include "Sequencer/StepLocks.h"   // LockSeed: a step's locks for this block
#include "Modulation/LastResolved.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <string_view>
#include <unordered_map>

// Generic per-lane modulation resolve — the family range-based routing form (the canonical
// `.prop` path, lifted from mu-on so any product builds it once). For each destination i:
//   1. seed its 0..1 proportion from the backing APVTS atom (range.convertTo0to1),
//   2. run the slot's ModulationMatrix under a try-lock (skipped on contention → zero-cost
//      when no modulators are assigned),
//   3. write the modulated value back into out[i] in the destination's value units
//      (range.convertFrom0to1, clamped 0..1).
// Audio-thread-only; allocation-free given a pre-sized `map`. `ids` / `atoms` / `ranges` are
// parallel arrays of length `count`; `out` must hold `count` floats. Returns false when the slot's
// matrix lock was missed (its modulators are being edited), so `out` holds the unmodulated values: a
// caller that keeps its last resolved values holds them for the block instead (C12).
// `lock` (optional): the destinations a step has locked. Their seed is the lock's proportion instead of the knob's; when the lock
// says modulation is off for them they are set back to the lock after the matrix runs.
namespace mu_mod {

inline bool resolveLane(Layer* slot, double beat, int count,
                        const char* const* ids,
                        const std::atomic<float>* const* atoms,
                        const juce::NormalisableRange<float>* ranges,
                        std::unordered_map<std::string_view, float>& map,
                        float* out,
                        const mu_core::LockSeed* lock = nullptr)
{
    for (int i = 0; i < count; ++i)
    {
        const float v = atoms[i] != nullptr ? atoms[i]->load(std::memory_order_relaxed)
                                            : ranges[i].start;
        map[ids[i]] = lock != nullptr && i < 32 && (lock->mask >> i & 1u) ? lock->value01[(size_t) i] : ranges[i].convertTo0to1(v);
    }

    bool resolved = true;
    if (slot != nullptr)
    {
        if (mu_core::trySpinLock(slot->modLock))
        {
            slot->modulationMatrix.process(slot->controlSequences, beat, map);
            mu_core::spinUnlock(slot->modLock);
        }
        else resolved = false;
    }

    if (lock != nullptr && ! lock->modulate)   // locked and unmodulated: the lock itself, whatever the matrix did
        for (int i = 0; i < count && i < 32; ++i)
            if (lock->mask >> i & 1u) map[ids[i]] = lock->value01[(size_t) i];

    for (int i = 0; i < count; ++i)
        out[i] = ranges[i].convertFrom0to1(juce::jlimit(0.0f, 1.0f, map[ids[i]]));
    return resolved;
}

} // namespace mu_mod
