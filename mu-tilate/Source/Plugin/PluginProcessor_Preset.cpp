#include "Plugin/PluginProcessor.h"
#include "Persistence/PresetFiles.h"   // mu-core: shared preset-file handling

// mu-Tilate preset I/O on the family composed state (mu-core LayerState): full presets (.muTilate), per-layer presets
// (.muPhrase), host sessions and layer reset all build and apply the same layer node.
namespace mu_tilate
{

// Describe the layers once (ctor): each layer's p{N}_ param prefix. Every layer preset, full preset and host session is
// built and applied from this. The loop reference, slices and timeline travel in each layer node through Phrase::writeExtras.
void PluginProcessor::setupLayerState()
{
    juce::StringArray prefixes;
    for (int l = 0; l < mu_limits::kMaxLayers; ++l) prefixes.add(layerParamPrefix(l));
    initLayerState(prefixes, {});
}

juce::ValueTree PluginProcessor::toLayerState(const juce::ValueTree& tree) const
{
    return toComposedState(tree, [](const juce::ValueTree&, juce::ValueTree&) {});
}

void PluginProcessor::applyStateTree(const juce::ValueTree& state)
{
    applyComposedState(state);
    syncAllFxParams();   // re-seed mixer/FX (unchanged values skip listeners)

    // A restored layer names its loop by path: load it again, keeping the slices and timeline the file carried. (Embedding
    // the loop in the preset is a later step.)
    for (int l = 0; l < mu_limits::kMaxLayers; ++l)
        reloadLoopIfNeeded(l);
}

void PluginProcessor::reloadLoopIfNeeded(int layer)
{
    const auto& ph = getPhrase(layer);
    const auto path = ph.loopPath();
    if (path.isEmpty()) return;
    const auto d = ph.getData();
    if (d != nullptr && d->audio != nullptr && ph.loadedLoopPath() == path) return;   // already holds that file's audio
    loadLoopAsync(layer, juce::File(path), true);
}

void PluginProcessor::useLoadedFullPreset(juce::ValueTree state)
{
    layerSwapper.useFull(toLayerState(state));
}

juce::ValueTree PluginProcessor::captureLayerPresetTree(int layer)
{
    if (layer < 0 || layer >= mu_limits::kMaxLayers) return {};
    return captureLayerNode(layer, kPhrasePresetTag);
}

void PluginProcessor::saveLayerPreset(int layer, const juce::String& name)
{
    if (auto xml = captureLayerPresetTree(layer).createXml())
        mu_pp::writeXmlAtomically(*xml, getLayerPresetDir().getChildFile(mu_pp::safePresetFileName(name, "Layer")
                                                                           + "." + getLayerPresetExtension()),
                                  onLoadError);
}

void PluginProcessor::loadLayerPreset(int layer, const juce::File& file)
{
    if (layer < 0 || layer >= mu_limits::kMaxLayers || ! file.existsAsFile()) return;
    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr || ! xml->hasTagName(kPhrasePresetTag))
    {
        if (onLoadError) onLoadError("Could not read \"" + file.getFileName() + "\"");
        return;
    }
    stageLayerPreset(layer, juce::ValueTree::fromXml(*xml), false, false);
}

void PluginProcessor::stageLayerPresetTree(int layer, juce::ValueTree tree, bool keepIdentity)
{
    juce::ignoreUnused(keepIdentity);   // a layer node carries no name or colour
    if (layer < 0 || layer >= mu_limits::kMaxLayers || ! tree.isValid()) return;
    layerSwapper.useSlot(layer, std::move(tree));
}

void PluginProcessor::applyLayerTree(int layer, const juce::ValueTree& tree)
{
    applyLayerNode(layer, tree);
    reloadLoopIfNeeded(layer);   // the preset names a loop: bring it in
    if (onLayerPresetCommitted) onLayerPresetCommitted(layer);
}

juce::File PluginProcessor::getLayerPresetDir() const { return getContentDir().getChildFile("Phrases"); }

} // namespace mu_tilate
