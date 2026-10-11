#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Sequencer/EuclidParamLayout.h"   // mu-core: the Euclid parameter set shared with mu-Clid

namespace mu_clid_midi {

// Parameter ids that are saved data: the "lite_" prefix is the product's old name and stays.
inline constexpr const char* kMidiNoteId   = "lite_midiNote";
inline constexpr const char* kAccentAmtId  = "lite_accentAmt";
inline constexpr const char* kMasterLoopId = "mstrLoop";   // read by mu-core's MasterLoopSection

// The only rhythm is slot 0, so its ids are "r0_..." exactly as in mu-Clid.
inline constexpr const char* kRhythmIdPrefix = "r0_";

// Declares the product's parameters: one rhythm's Euclid set, the master loop and the two
// MIDI-output controls. A MIDI effect has no audio, so there are no voice, mixer or FX parameters.
inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    mu_core::addEuclidParams(layout, kRhythmIdPrefix, "Rhythm 1 ");

    layout.add(std::make_unique<juce::AudioParameterInt>(kMasterLoopId, "Master Loop", 0, 16, 0));   // 0 = free, 1-16 -> 16-256 steps
    layout.add(std::make_unique<juce::AudioParameterInt>(kMidiNoteId, "MIDI Note", 0, 127, 36));
    layout.add(std::make_unique<juce::AudioParameterFloat>(kAccentAmtId, "Accent",
                                                           juce::NormalisableRange<float>(0.0f, 100.0f), 0.0f));
    return layout;
}

} // namespace mu_clid_midi
