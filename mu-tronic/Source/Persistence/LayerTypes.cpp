#include "Persistence/LayerTypes.h"

namespace mu_tronic
{

void registerLayerTypes(mu_core::LayerTypeRegistry& registry)
{
    // The empty unit: no layer library is linked yet, so a slot can only be Empty. A type is added here as
    //     registry.add(mu_toni::arpLayerTypeInfo());
    // once its library is in CMakeLists.txt; nothing else in mu-Tronic names the type.
    juce::ignoreUnused(registry);
}

} // namespace mu_tronic
