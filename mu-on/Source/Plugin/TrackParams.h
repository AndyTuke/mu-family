#pragma once

#include <juce_core/juce_core.h>
#include <utility>
#include <vector>

// The fixed mu-On instrument lanes — shared by the processor, the engines, and the UI.
// Kick/Bass/Hat/Snare are step-sequenced instruments (kNumTracks in the StepPattern);
// Rumble is a processor lane (no step row) — it takes the Kick's audio as a feed and runs
// it through drive/delays/reverb/envelope/filter.
namespace mu_on
{
enum Channel { Kick = 0, Bass = 1, Hat = 2, Snare = 3, Rumble = 4, kNumChannels = 5 };

// Each lane's engine parameters share a prefix "t{lane}_" (t = Track, the index is the layer slot):
// the one place mu-On spells it. Saved data keys on these ids, so they never change again.
constexpr const char* layerParamPrefix(int lane) noexcept
{
    constexpr const char* kPrefix[kNumChannels] = { "t0_", "t1_", "t2_", "t3_", "t4_" };
    return kPrefix[lane < 0 ? 0 : (lane >= kNumChannels ? kNumChannels - 1 : lane)];
}

// The prefixes before the rename (by lane type: Kick, Bass, Hat, Snare, Rumble). Read-only, so older saved
// data still loads; never used to name a live parameter.
constexpr const char* legacyLayerParamPrefix(int lane) noexcept
{
    constexpr const char* kLegacy[kNumChannels] = { "k_", "b_", "h_", "s_", "r_" };
    return kLegacy[lane < 0 ? 0 : (lane >= kNumChannels ? kNumChannels - 1 : lane)];
}

inline juce::String layerParamId(int lane, const juce::String& suffix)
{
    return juce::String(layerParamPrefix(lane)) + suffix;
}

// The pre-rename prefixes in lane order (for LayerLayout::addLegacyPrefixes).
inline juce::StringArray legacyLayerParamPrefixes()
{
    juce::StringArray prefixes;
    for (int l = 0; l < kNumChannels; ++l) prefixes.add(legacyLayerParamPrefix(l));
    return prefixes;
}

// {old, new} prefix pairs in lane order (for MidiControlMap::setIdPrefixRenames).
inline std::vector<std::pair<juce::String, juce::String>> layerParamPrefixRenames()
{
    std::vector<std::pair<juce::String, juce::String>> renames;
    for (int l = 0; l < kNumChannels; ++l) renames.emplace_back(legacyLayerParamPrefix(l), layerParamPrefix(l));
    return renames;
}

// Number of step-sequenced lanes (Kick..Snare). Rumble has no step row.
inline constexpr int kNumStepLanes = Rumble;   // = 4
}
