#include "Plugin/PluginProcessor.h"
#include "Persistence/PresetFiles.h"   // mu-core: shared preset-file handling

// mu-Tronic preset I/O on the family composed state (mu-core LayerState): full presets (.muTronic) and the standalone
// session are the same state tree, with each slot's type and name added to its node (Persistence/TronicState.h).
// These are all PluginProcessor:: members.

namespace mu_tronic
{

// Describe the slots once (constructor): the composed state is built and applied from this.
void PluginProcessor::setupLayerState()
{
    initLayerStatePrefixSets(slotPrefixSets());
}

// Any saved state in the composed shape (an older APVTS dump is rebuilt; mu-Tronic has none, so this is the identity).
juce::ValueTree PluginProcessor::toLayerState(const juce::ValueTree& tree) const
{
    return toComposedState(tree);
}

// The composed state plus, on each slot node, its layer's type name and (when the user typed one) its name.
juce::ValueTree PluginProcessor::captureState()
{
    auto state = captureComposedState();
    auto slots = state.getChildWithName(mu_pp::kLayersTag);
    for (int i = 0; i < slots.getNumChildren(); ++i)
    {
        auto node = slots.getChild(i);
        const int idx = (int) node.getProperty("idx", -1);
        const auto* layer = getLayer(idx);
        if (layer == nullptr) continue;
        node.setProperty("type", mu_core::layerTypeName((mu_core::LayerTypeId) layer->typeId()), nullptr);
        if (! layer->name.empty()) node.setProperty("name", juce::String(layer->name), nullptr);
    }
    return state;
}

// Apply a composed state (a session restore, a full-preset load or its boundary commit), then re-seed the engines.
// Each slot's type is set first, so the rows and extras land on a layer of the right type; a type this build does
// not offer becomes Empty. A changed type is a setLayerType: published without muting, the old layer sounding out.
void PluginProcessor::applyStateTree(const juce::ValueTree& state)
{
    for (int s = 0; s < mu_limits::kMaxLayers; ++s)
    {
        const auto node = mu_pp::findLayerNode(state, s);
        auto type = slotNodeType(node);
        if (type != mu_core::LayerTypeId::Empty && layerTypes.find(type) == nullptr)
            type = mu_core::LayerTypeId::Empty;   // a type this build does not offer
        // The slot's rows go in with the layer, before it is published, so a state applied during playback never
        // sounds a new layer at its defaults. (All slots landing in the same block is the scene commit's job.)
        setLayerType(s, type, node.isValid() ? &node : nullptr);
        if (auto* layer = getLayer(s))
            layer->name = node.getProperty("name").toString().toStdString();   // absent = the default name
    }
    applyComposedState(state);
    syncAllFxParams();   // re-seed mixer/FX (unchanged values skip listeners)
}

// While playing, stage a loaded full preset for the next boundary (the shared swapper commit); while stopped,
// apply it now. Converted to the composed shape here, so the commit does no parsing.
void PluginProcessor::useLoadedFullPreset(juce::ValueTree state)
{
    layerSwapper.useFull(toLayerState(state));
}

} // namespace mu_tronic
