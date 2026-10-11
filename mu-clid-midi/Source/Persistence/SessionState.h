#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace mu_clid_midi::session {

// The host session (the DAW project's saved plugin state) as text: the parameter tree.
juce::String toXmlString(juce::AudioProcessorValueTreeState& apvts);

// Restores a session written by this product, or by mu-Clid Lite, which saved a full mu-Clid
// preset tree. Returns false with `error` set when the text is not readable state. Message thread only.
bool restoreFromXmlString(const juce::String& xml, juce::AudioProcessorValueTreeState& apvts, juce::String& error);

// Applies the parts of a saved mu-Clid preset tree (root "MuClidPreset") that this product has
// parameters for: the first rhythm's Euclid values, the master loop and the per-parameter rows.
void applyLegacySession(const juce::ValueTree& root, juce::AudioProcessorValueTreeState& apvts);

} // namespace mu_clid_midi::session
