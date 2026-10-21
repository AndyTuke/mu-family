#pragma once

#include "Sequencer/LayerTypeInfo.h"

class ProcessorBase;

namespace mu_tronic
{

// The Rhythm layer type as mu-Tronic offers it: mu-Clid's description of the type (its parameter table, its layer, its
// .muRhythm conversion) plus what a slot host adds: the type's services over ProcessorBase (the sample library, the
// layers' mixer strips, the applying of a changed parameter row to its layer), the Rhythm content and sidebar picture
// built over the same adapter, the type's paint colour (mu-Clid's purple) and its header tag ("Clid"). The one place
// mu-Tronic names the Rhythm's layer library. The content and picture factories exist in the app target only
// (MU_TRONIC_WITH_UI), as for the other types.
mu_core::LayerTypeInfo rhythmLayerType();

// True when the slot holds a Rhythm set to send MIDI notes instead of playing its sample: its mixer strip is then drawn
// inactive, as mu-Clid draws it.
bool isRhythmMidiOutput(const ProcessorBase& host, int slot);

// A Rhythm the user has just created from the app's default file: if that file's sample cannot be found, the layer
// starts with no sample (the bar's ordinary empty state) rather than a "Missing" warning for a file nobody chose.
// Loaded data keeps the warning, so only the creation path calls this.
void clearUnresolvedSample(ProcessorBase& host, int slot);

} // namespace mu_tronic
