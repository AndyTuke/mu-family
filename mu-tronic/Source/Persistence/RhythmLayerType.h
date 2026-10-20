#pragma once

#include "Sequencer/LayerTypeInfo.h"

namespace mu_tronic
{

// The Rhythm layer type as mu-Tronic offers it: mu-Clid's description of the type (its parameter table, its layer, its
// .muRhythm conversion) plus what a slot host adds: the type's services over ProcessorBase (the sample library, the
// layers' mixer strips, the applying of a changed parameter row to its layer), the Rhythm content and sidebar picture
// built over the same adapter, the type's paint colour (mu-Clid's purple) and its header tag ("Clid"). The one place
// mu-Tronic names the Rhythm's layer library. The content and picture factories exist in the app target only
// (MU_TRONIC_WITH_UI), as for the other types.
mu_core::LayerTypeInfo rhythmLayerType();

} // namespace mu_tronic
