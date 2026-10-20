#include "Persistence/LayerTypes.h"
#include "Persistence/ArpLayerType.h"   // the Arp type (mu-Toni's layer library)

namespace mu_tronic
{

void registerLayerTypes(mu_core::LayerTypeRegistry& registry)
{
    // One line per type, in chooser order; its library is linked in CMakeLists.txt and nothing else in mu-Tronic
    // names the type. Empty is not listed: every slot can hold it.
    registry.add(arpLayerType());
}

} // namespace mu_tronic
