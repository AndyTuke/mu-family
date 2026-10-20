#include "Persistence/LayerTypes.h"
#include "Persistence/ArpLayerType.h"     // the Arp type (mu-Toni's layer library)
#include "Persistence/TrackLayerTypes.h"   // Kick, Bass, Hat, Snare, Rumble (mu-On's layer library)
#include "Persistence/PatternLayerType.h"  // the Pattern type (mu-Tant's layer library)
#include "Persistence/RhythmLayerType.h"   // the Rhythm type (mu-Clid's layer library)

namespace mu_tronic
{

void registerLayerTypes(mu_core::LayerTypeRegistry& registry)
{
    // One line per type, in chooser order; its library is linked in CMakeLists.txt and nothing else in mu-Tronic
    // names the type. Empty is not listed: every slot can hold it.
    registry.add(arpLayerType());
    registry.add(trackLayerType(mu_core::LayerTypeId::Kick));
    registry.add(trackLayerType(mu_core::LayerTypeId::Bass));
    registry.add(trackLayerType(mu_core::LayerTypeId::Hat));
    registry.add(trackLayerType(mu_core::LayerTypeId::Snare));
    registry.add(trackLayerType(mu_core::LayerTypeId::Rumble));
    registry.add(patternLayerType());
    registry.add(rhythmLayerType());
}

} // namespace mu_tronic
