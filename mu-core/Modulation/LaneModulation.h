#pragma once

#include "Sequencer/VoiceSlot.h"

#include <juce_audio_basics/juce_audio_basics.h>
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
// parallel arrays of length `count`; `out` must hold `count` floats.
namespace mu_mod {

inline void resolveLane(VoiceSlot* slot, double beat, int count,
                        const char* const* ids,
                        const std::atomic<float>* const* atoms,
                        const juce::NormalisableRange<float>* ranges,
                        std::unordered_map<std::string_view, float>& map,
                        float* out)
{
    for (int i = 0; i < count; ++i)
    {
        const float v = atoms[i] != nullptr ? atoms[i]->load(std::memory_order_relaxed)
                                            : ranges[i].start;
        map[ids[i]] = ranges[i].convertTo0to1(v);
    }

    if (slot != nullptr)
    {
        bool expected = false;
        if (slot->modLock.compare_exchange_strong(expected, true, std::memory_order_acquire))
        {
            slot->modulationMatrix.process(slot->controlSequences, beat, map);
            slot->modLock.store(false, std::memory_order_release);
        }
    }

    for (int i = 0; i < count; ++i)
        out[i] = ranges[i].convertFrom0to1(juce::jlimit(0.0f, 1.0f, map[ids[i]]));
}

} // namespace mu_mod
