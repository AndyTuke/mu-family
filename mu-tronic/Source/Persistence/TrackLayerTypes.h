#pragma once

#include "Sequencer/LayerTypeInfo.h"

#include <functional>

class ProcessorBase;

namespace mu_tronic
{

// The five Track layer types (Kick, Bass, Hat, Snare, Rumble) as mu-Tronic offers them: mu-On's description of the
// type (its parameter table, its locked Track, its preset folder) plus what a slot host adds: the Track content and
// sidebar picture, built over one host adapter on ProcessorBase for all four, and the type's paint colour (mu-On's
// yellow) and header tag ("On"). The one place mu-Tronic names the Track's UI. `type` must be one of the five.
// The content and picture factories exist in the app target only (MU_TRONIC_WITH_UI), as for the Arp.
mu_core::LayerTypeInfo trackLayerType(mu_core::LayerTypeId type);

// A Rumble's source (its `t{N}_src` row) is a slot number, so it follows that slot's layer: called from the processor's
// remapLayerReferences hook, it rewrites every Rumble slot's source through `oldToNew` (-1: the layer is gone). A gone
// source falls to the first filled slot that is not a Rumble, else slot 0 (which then plays silence).
void remapRumbleSources(ProcessorBase& host, const std::function<int(int)>& oldToNew);

// Message thread, after a slot became a Rumble: points its source at the first filled slot that is not a Rumble, if
// it has none that works (itself, an Empty slot or another Rumble); the Rumble stays silent until a source exists.
void chooseRumbleSource(ProcessorBase& host, int slot);

} // namespace mu_tronic
