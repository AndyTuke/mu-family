#pragma once

#include "Sequencer/LayerTypeInfo.h"

namespace mu_tronic
{

// The Arp layer type as mu-Tronic offers it: mu-Toni's description of the type (its parameter table, its layer) plus
// what a slot host adds: the Arp content and sidebar picture, built over a host adapter on ProcessorBase, the
// type's paint colour (mu-Toni's blue) and its header tag ("Toni"). The one place mu-Tronic names the Arp's UI.
// The content and picture factories exist in the app target only (MU_TRONIC_WITH_UI): the console test target has
// none of the UI they need, and its processor never builds a UI.
mu_core::LayerTypeInfo arpLayerType();

} // namespace mu_tronic
