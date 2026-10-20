#pragma once

#include "Sequencer/LayerTypeRegistry.h"

namespace mu_tronic
{

// The layer types this build of mu-Tronic offers a slot, one line per type, in chooser order (the groups are
// the products). Each type's layer library (mu-<product>-layer) is linked in CMakeLists.txt and registered
// here: this file is the only mu-Tronic source that names a layer type's library. Registered: Arp (mu-Toni), Kick, Bass, Hat, Snare (mu-On).
void registerLayerTypes(mu_core::LayerTypeRegistry& registry);

} // namespace mu_tronic
