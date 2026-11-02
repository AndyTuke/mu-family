#pragma once

#include "Sequencer/SliceList.h"
#include "Sequencer/Timeline.h"

#include <juce_data_structures/juce_data_structures.h>

// How a Phrase layer's non-parameter data is saved: the slice markers and the timeline blocks as ValueTree children
// of the layer node (docs/mu-tilate/design.md, "Persistence"), the loop as a reference. Everything the user can edit is
// here; the audio itself is never in the tree (it is embedded by the preset layer or found by path).
namespace mu_tilate
{

// The tag of the child node that holds the Phrase data, and the property names. Saved data keys on these.
inline constexpr const char* kPhraseNodeTag = "Phrase";

// The saved form of one layer's loop reference, slices and timeline.
struct PhraseState
{
    juce::String loopPath;            // where the loop was loaded from (empty = none)
    juce::String loopName;            // its display name
    int          loopLength = 0;      // samples at the loop's own rate: markers are only valid against the same length (of the used region)
    int          regionStart = 0;     // the used section of the source file, in its samples; an end of 0 means the whole file
    int          regionEnd = 0;
    SliceList    slices { 0 };
    Timeline     timeline;
};

// Adds (or replaces) the Phrase child of `node` from `state`.
void writePhraseState(juce::ValueTree& node, const PhraseState& state);

// Reads the Phrase child of `node`; a missing child or property is the default ("missing means default"). Markers and
// blocks outside what the SliceList and Timeline accept are dropped by their own rules.
PhraseState readPhraseState(const juce::ValueTree& node);

} // namespace mu_tilate
