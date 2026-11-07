#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Persistence/ArpParams.h"        // mu-toni: addArpRows, the vpi rows
#include "Sequencer/MidiArpCore.h"        // the parameter ids

namespace mu_toni_midi
{

// The product's parameters: mu-Toni's arp rows for slot 0 (a0_scale ... a0_stepMask, the same ids and ranges as mu-Toni, so a
// layer preset can be shared later), the MIDI In channel (the id mu-Toni uses), the MIDI Out channel and the note velocity.
// A MIDI effect has no sound, so there are no oscillator, filter, envelope, insert or mixer parameters.
inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    mu_toni::addArpRows(layout, 0);
    layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{ kMidiInChId, 1 },  "MIDI In Channel",  0, 16, 0));    // 0 = Omni
    layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{ kMidiOutChId, 1 }, "MIDI Out Channel", 1, 16, 1));
    layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{ kMidiVelId, 1 },   "Velocity",         1, 127, 100));
    return layout;
}

} // namespace mu_toni_midi
