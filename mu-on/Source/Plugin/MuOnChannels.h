#pragma once

#include <juce_core/juce_core.h>

// The fixed mu-On instrument lanes — shared by the processor, the engines, and the UI.
// Kick/Bass/Hat/Snare are step-sequenced instruments (kNumTracks in the StepPattern);
// Rumble is a processor lane (no step row) — it takes the Kick's audio as a feed and runs
// it through drive/delays/reverb/envelope/filter.
namespace mu_on
{
enum Channel { Kick = 0, Bass = 1, Hat = 2, Snare = 3, Rumble = 4, kNumChannels = 5 };

// Each lane's engine parameters share a letter prefix ("k_", "b_", ...): the one place mu-On spells it.
// Saved data keys on these ids, so they never change.
constexpr const char* layerParamPrefix(int lane) noexcept
{
    constexpr const char* kPrefix[kNumChannels] = { "k_", "b_", "h_", "s_", "r_" };
    return kPrefix[lane < 0 ? 0 : (lane >= kNumChannels ? kNumChannels - 1 : lane)];
}

inline juce::String layerParamId(int lane, const juce::String& suffix)
{
    return juce::String(layerParamPrefix(lane)) + suffix;
}

// Number of step-sequenced lanes (Kick..Snare). Rumble has no step row.
inline constexpr int kNumStepLanes = Rumble;   // = 4
}
