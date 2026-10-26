#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// The Rhythm layer type's parameter table (docs/mu-tronic/design.md 2.3): the per-slot rows under the slot's
// r{N}_ prefix (the Euclid set, pitch, filter, amp, insert and MIDI output). Saved data and DAW automation key
// on these ids and their order, so rows are only ever appended. The ch{N}_ mixer strips are not the type's.
namespace mu_clid
{

// The root tag of a Rhythm layer-preset file (.muRhythm), shared by mu-Clid and any host that saves or loads one.
inline constexpr const char* kRhythmPresetTag = "MuClidRhythm";

// The Rhythm type's rows for one layer slot (r{slot}_*). Slots below mu_limits::kMaxAutomatedChannels get
// the long "Rhythm N" names the DAW's automation lanes show; the others the short "RN".
void addLayerParams(juce::AudioProcessorValueTreeState::ParameterLayout& layout, int slot);

} // namespace mu_clid
