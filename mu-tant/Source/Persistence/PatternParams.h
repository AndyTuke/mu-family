#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// The Pattern layer type's parameter table (docs/mu-tronic/design.md 2.3): the per-slot rows under the
// slot's v{N}_ prefix and the two tonal-centre rows every Pattern reads (root and scale). Saved data and
// DAW automation key on these ids and their order, so rows are only ever appended.
namespace mu_tant
{

// The one place mu-Tant spells a layer's APVTS id: "v{slot}_{suffix}" (v = the Pattern type's letter, kept
// from mu-Tant's first release because the ids are users' automation targets).
inline constexpr const char* kLayerParamLetter = "v";

inline juce::String layerParamPrefix(int slot)
{
    return juce::String(kLayerParamLetter) + juce::String(slot) + "_";
}

inline juce::String layerParamId(int slot, const juce::String& suffix)
{
    return layerParamPrefix(slot) + suffix;
}

// The tonal centre every Pattern quantises to (global rows, not per slot).
inline constexpr const char* kRootParamId  = "root";
inline constexpr const char* kScaleParamId = "scale";

// The Pattern type's rows for one layer slot (v{slot}_*): oscillators, cross-mod, levels, the two filters,
// the pitch-envelope depths, the slot level, the gate and the insert.
void addLayerParams(juce::AudioProcessorValueTreeState::ParameterLayout& layout, int slot);

// The Pattern type's global rows: the shared tonal centre (root, then scale).
void addPatternGlobalParams(juce::AudioProcessorValueTreeState::ParameterLayout& layout);

} // namespace mu_tant
