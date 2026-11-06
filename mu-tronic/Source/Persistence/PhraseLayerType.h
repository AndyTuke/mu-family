#pragma once

#include "Sequencer/LayerTypeInfo.h"

namespace mu_tronic
{

// The Phrase layer type as mu-Tronic offers it: mu-Tilate's description of the type (its parameter table, its layer) plus what a
// slot host adds: the services (the loop loading, slicing and auditions, shared by every Phrase slot, and the restore of a
// loop a preset or session names), the Phrase content and sidebar picture built over them, the type's paint colour and its
// header tag ("Tilate"). The one place mu-Tronic names the Phrase's layer library. The content and picture factories exist in
// the app target only (MU_TRONIC_WITH_UI).
mu_core::LayerTypeInfo phraseLayerType();

} // namespace mu_tronic
