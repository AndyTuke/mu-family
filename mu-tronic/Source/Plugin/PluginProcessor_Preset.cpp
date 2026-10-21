#include "Plugin/PluginProcessor.h"
#include "Persistence/PresetFiles.h"   // mu-core: shared preset-file handling
#include "Persistence/ProductImport.h" // reading a product's full preset as layers
#include "Persistence/TrackLayerTypes.h" // chooseRumbleSource

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

// Import a product's full preset into the Empty slots (see the header). Two steps: plan, which refuses before anything
// changes, then lay each layer into its slot through the one path a loaded layer preset uses (setLayerType with the node).
PluginProcessor::ImportResult PluginProcessor::importProductPreset(const juce::File& file)
{
    ImportResult result;
    const auto imported = readProductPreset(file, layerTypes);
    if (! imported.ok())
    {
        result.message = imported.error.isNotEmpty() ? imported.error : "Could not read " + file.getFileName();
        return result;
    }

    // The free slots, in order: an Empty slot is the only kind an import may use.
    std::vector<int> freeSlots;
    bool hasPattern = false;
    for (int s = 0; s < mu_limits::kMaxLayers; ++s)
    {
        const auto type = getLayerTypeId(s);
        if (type == mu_core::LayerTypeId::Empty) freeSlots.push_back(s);
        hasPattern = hasPattern || type == mu_core::LayerTypeId::Pattern;
    }
    const int needed = (int) imported.entries.size();
    if (needed > (int) freeSlots.size())
    {
        result.message = "Needs " + juce::String(needed) + " free slots, " + juce::String((int) freeSlots.size()) + " free";
        return result;
    }

    // Where each product layer lands, for the sidechain sources and a Rumble's source.
    std::vector<int> slotOfSource((size_t) kProductLayers, -1);
    for (int i = 0; i < needed; ++i)
        slotOfSource[(size_t) imported.entries[(size_t) i].sourceIndex] = freeSlots[(size_t) i];

    // Phase 1: the layers, each with the preset's own values as its first state.
    for (int i = 0; i < needed; ++i)
    {
        auto layer = imported.entries[(size_t) i];
        const int slot = freeSlots[(size_t) i];
        if (layer.type == mu_core::LayerTypeId::Rumble && layer.rumbleSource >= 0 && layer.rumbleSource < kProductLayers
            && slotOfSource[(size_t) layer.rumbleSource] >= 0)
            setRumbleSourceRow(layer.node, slotOfSource[(size_t) layer.rumbleSource]);
        if (! setLayerType(slot, layer.type, &layer.node)) continue;
        if (auto* l = getLayer(slot)) l->name = layer.name.toStdString();
        ++result.imported;
    }

    // Phase 2: a Rumble whose source was not imported (or is not usable) listens to the first filled slot that can be one.
    for (int i = 0; i < needed; ++i)
        if (imported.entries[(size_t) i].type == mu_core::LayerTypeId::Rumble)
            chooseRumbleSource(*this, freeSlots[(size_t) i]);

    // Phase 3: the mixer strips, then the Pattern layers' shared tonal centre when this import brings the first Pattern.
    for (int i = 0; i < needed; ++i)
    {
        const int slot = freeSlots[(size_t) i];
        for (const auto& row : imported.entries[(size_t) i].strip)
        {
            auto* param = apvts.getParameter("ch" + juce::String(slot) + "_" + row.suffix);
            if (param == nullptr) continue;
            double actual = row.normalised ? (double) param->convertFrom0to1((float) row.value) : row.value;
            if (row.suffix == "scSrc") actual = remapImportedSidechain((int) std::lround(row.value), slot, slotOfSource);
            param->setValueNotifyingHost(param->convertTo0to1((float) actual));
        }
    }
    if (! hasPattern)
        for (const auto& g : imported.globals)
            if (auto* param = apvts.getParameter(g.id))
                param->setValueNotifyingHost(param->convertTo0to1((float) g.value));

    if (onLayerPresetCommitted)
        for (int i = 0; i < needed; ++i) onLayerPresetCommitted(freeSlots[(size_t) i]);

    result.ok = result.imported == needed;
    result.message = "Imported " + juce::String(result.imported) + (result.imported == 1 ? " layer from " : " layers from ")
                   + imported.presetName;
    return result;
}

} // namespace mu_tronic
