#pragma once

#include "Sequencer/LayerTypeInfo.h"

namespace mu_tronic
{

// The four non-Rumble Track layer types (Kick, Bass, Hat, Snare) as mu-Tronic offers them: mu-On's description of the
// type (its parameter table, its locked Track, its preset folder) plus what a slot host adds: the Track content and
// sidebar picture, built over one host adapter on ProcessorBase for all four, and the type's paint colour (mu-On's
// yellow) and header tag ("On"). The one place mu-Tronic names the Track's UI. `type` must be one of the four.
// The content and picture factories exist in the app target only (MU_TRONIC_WITH_UI), as for the Arp.
mu_core::LayerTypeInfo trackLayerType(mu_core::LayerTypeId type);

} // namespace mu_tronic
