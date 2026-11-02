#pragma once

#include "Audio/AlgorithmNames.h"   // mu_audio::kInsertAlgorithmCount
#include "Audio/VoiceParams.h"      // VoiceParams::kInsertSlotCount

#include <juce_audio_processors/juce_audio_processors.h>

// The insert effect's parameter rows, kept apart from the mixer's global rows (Plugin/MixerFxParams.h) so a layer
// library can declare its own insert without including anything from Plugin/ (tests/scripts/check-layer-boundary.ps1).
namespace mu_mixfx {

// One insert's parameters under `prefix` (e.g. "bus0_i1_"): the algorithm and the four generic slot
// knobs. The suffixes and ranges are the layer insert's (drvChar, insP1..insP4, stored normalised 0..1),
// so the shared insert UI and the engine read a bus insert exactly as they read a layer's.
inline void addInsertParams(juce::AudioProcessorValueTreeState::ParameterLayout& layout,
                            const juce::String& prefix, const juce::String& namePrefix = {}, int versionHint = 1)
{
    layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID { prefix + "drvChar", versionHint },
                   namePrefix + "Insert Algo", 0, mu_audio::kInsertAlgorithmCount - 1, 0));
    for (int p = 1; p <= VoiceParams::kInsertSlotCount; ++p)
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { prefix + "insP" + juce::String(p), versionHint },
                       namePrefix + "Insert P" + juce::String(p), juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    // Tempo sync for the algorithms that offer it (Tape Delay Time, Tape Spring Rate): sixteenth-note steps.
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { prefix + "insSync", versionHint },
                   namePrefix + "Insert Sync", false));
}

} // namespace mu_mixfx
