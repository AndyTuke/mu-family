#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace mu_clid_midi::session {

// The host session (the DAW project's saved plugin state) as text: the parameter tree.
juce::String toXmlString(juce::AudioProcessorValueTreeState& apvts);

// The rhythm name and colour an old state carried; they are not parameters, so the caller puts them
// on the Rhythm. Empty name / negative colour = the state had none.
struct Identity
{
    juce::String name;
    int          colourIndex = -1;
};

// Sets every parameter to its default, so a restore never inherits values from the instance's
// previous state. Message thread only.
void resetToDefaults(juce::AudioProcessorValueTreeState& apvts);

// Restores a session written by this product, or by mu-Clid Lite, which saved a full mu-Clid
// preset tree. The instance is reset to defaults first, so a key the session lacks ends up at its
// default. Returns false with `error` set when the text is not readable state (nothing is changed
// then). `identity`, when given, receives the old name and colour. Message thread only.
bool restoreFromXmlString(const juce::String& xml, juce::AudioProcessorValueTreeState& apvts, juce::String& error,
                          Identity* identity = nullptr);

// Applies the parts of a saved mu-Clid preset tree (root "MuClidPreset") that this product has
// parameters for: the first rhythm's Euclid values, the master loop and the per-parameter rows.
void applyLegacySession(const juce::ValueTree& root, juce::AudioProcessorValueTreeState& apvts);

} // namespace mu_clid_midi::session
