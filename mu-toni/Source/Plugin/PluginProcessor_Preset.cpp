#include "Plugin/PluginProcessor.h"
#include "Modulation/ModulatorSerialise.h"   // mu-core: modulator (de)serialise + per-voice session data
#include "Persistence/PresetFiles.h"         // mu-core: shared preset-file handling

// mu-Toni preset I/O on the family composed state (mu-core LayerState): full presets (.muToni),
// per-layer presets (.muArp), host sessions and layer reset all build / apply the same layer node. Split out of PluginProcessor.cpp to
// mirror mu-Tant's PluginProcessor_Preset.cpp. These are all PluginProcessor:: members.

namespace mu_toni
{

namespace
{
    constexpr const char* kLayerPresetTag = "MuToniLayer";

}

// Describe the layers once (ctor): each layer's a{N}_ param prefix (its modulators save through Arp). Every layer
// preset, full preset and host session is built and applied from this.
void PluginProcessor::setupLayerState()
{
    juce::StringArray prefixes;
    for (int l = 0; l < mu_limits::kMaxLayers; ++l) prefixes.add(layerParamPrefix(l));
    initLayerState(prefixes, legacyLayerParamPrefixes(mu_limits::kMaxLayers));   // older APVTS dumps used "v{N}_"
}

// Any saved state (host session or full preset, either format) in the composed shape. Sessions
// saved before the shared modulator format kept them in <MuToniMods>, one <Modulators voice="N">
// per voice: those move into their layers.
juce::ValueTree PluginProcessor::toLayerState(const juce::ValueTree& tree) const
{
    return toComposedState(tree, [](const juce::ValueTree& child, juce::ValueTree& composed)
    {
        if (! child.hasType("MuToniMods")) return;
        for (int c = 0; c < child.getNumChildren(); ++c)
        {
            const auto mods = child.getChild(c);
            auto node = mu_pp::findLayerNode(composed, (int) mods.getProperty("voice", -1));
            if (node.isValid() && ! node.getChildWithName("Modulators").isValid())
                node.appendChild(mods.createCopy(), nullptr);
        }
    });
}

// Apply a composed state (host restore, full-preset load or its bar-line commit), then re-seed
// the mixer / FX engines.
void PluginProcessor::applyStateTree(const juce::ValueTree& state)
{
    applyComposedState(state);
    syncAllFxParams();   // re-seed mixer/FX (unchanged values skip listeners)
}

// While playing, stage a loaded full preset for the next bar line (the shared swapper commit); while
// stopped, apply it now. Converted to the composed shape here, so the commit does no parsing.
void PluginProcessor::useLoadedFullPreset(juce::ValueTree state)
{
    layerSwapper.useFull(toLayerState(state));
}

// A layer preset: the layer's node (prefix-free param rows + modulators), so it loads into any layer.
void PluginProcessor::saveLayerPreset(int layer, const juce::String& name)
{
    if (layer < 0 || layer >= mu_limits::kMaxLayers) return;
    if (auto xml = captureLayerNode(layer, kLayerPresetTag).createXml())
        mu_pp::writeXmlAtomically(*xml, getLayerPresetDir().getChildFile(mu_pp::safePresetFileName(name, "Layer")
                                                                           + "." + getLayerPresetExtension()),
                                  onLoadError);
}

void PluginProcessor::loadLayerPreset(int layer, const juce::File& file)
{
    if (layer < 0 || layer >= mu_limits::kMaxLayers || ! file.existsAsFile()) return;
    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr || ! xml->hasTagName(kLayerPresetTag))
    {
        if (onLoadError) onLoadError("Could not read \"" + file.getFileName() + "\"");
        return;
    }
    // While playing, stage it for the next bar line (the shared swapper commit); while stopped, apply now.
    layerSwapper.useSlot(layer, juce::ValueTree::fromXml(*xml));
}

// Apply a layer node (the stopped load and the bar-line commit), then tell the editor.
void PluginProcessor::applyLayerTree(int layer, const juce::ValueTree& tree)
{
    applyLayerNode(layer, tree);
    if (onLayerPresetCommitted) onLayerPresetCommitted(layer);
}

juce::File PluginProcessor::getLayerPresetDir() const { return getContentDir().getChildFile("Arps"); }

} // namespace mu_toni
