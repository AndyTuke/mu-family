#include "Plugin/PluginProcessor.h"
#include "Modulation/ModulatorSerialise.h"   // mu-core: modulator (de)serialise + per-voice session data
#include "Persistence/PresetFiles.h"         // mu-core: shared preset-file handling

// mu-Toni preset I/O: full presets (.muToni), per-layer presets (.muArp), layer reset and the
// state apply shared by host restore + full-preset load. Split out of PluginProcessor.cpp to
// mirror mu-Tant's PluginProcessor_Preset.cpp. These are all PluginProcessor:: members.

namespace mu_toni
{

namespace
{
    constexpr const char* kFullPresetTag  = "MuToniPreset";
    constexpr const char* kLayerPresetTag = "MuToniLayer";

    juce::String layerPrefix(int layer) { return "v" + juce::String(layer) + "_"; }
}

// Apply a full state tree (host session or full preset): convert the pre-standard modulator
// shape, swap the params, re-seed the mixer/FX and restore each layer's modulators.
void PluginProcessor::applyStateTree(juce::ValueTree tree)
{
    // Sessions saved before the shared format kept modulators in <MuToniMods>, one
    // <Modulators voice="N"> per voice: move them into the shared <VoiceData> shape.
    if (auto legacy = tree.getChildWithName("MuToniMods"); legacy.isValid())
    {
        juce::ValueTree channels(mu_pp::kChannelDataTag);
        for (int c = 0; c < legacy.getNumChildren(); ++c)
        {
            juce::ValueTree node(mu_pp::kChannelNodeTag);
            node.setProperty("idx", legacy.getChild(c).getProperty("voice", -1), nullptr);
            node.addChild(legacy.getChild(c).createCopy(), -1, nullptr);
            channels.addChild(node, -1, nullptr);
        }
        tree.removeChild(legacy, nullptr);
        tree.removeChild(tree.getChildWithName(mu_pp::kChannelDataTag), nullptr);
        tree.addChild(channels, -1, nullptr);
    }

    apvts.replaceState(tree);
    syncAllFxParams();   // re-seed mixer/FX (unchanged values skip listeners)
    mu_pp::readChannelModulators(apvts.state, kNumChannels,
        [this](int v) -> VoiceSlot& { return voiceSlots[(size_t) v]; },
        [](int, const std::string& id) { return mu_toni::isValidModDest(id); });
}

// The params + every layer's modulators, as saved in a session or a full preset.
juce::ValueTree PluginProcessor::captureState()
{
    auto state = apvts.copyState();
    state.removeChild(state.getChildWithName("MuToniMods"), nullptr);   // drop any pre-standard copy
    mu_pp::writeChannelData(state, kNumChannels, [this](int v) -> VoiceSlot& { return voiceSlots[(size_t) v]; });
    return state;
}

void PluginProcessor::savePreset(const juce::String& name, const juce::String& desc,
                                 const juce::String& category, bool /*embedSamples*/)
{
    mu_pp::writeFullPreset(getPresetsDir(), getFullPresetExtension(), kFullPresetTag,
                           name, desc, category, captureState());
}

void PluginProcessor::loadPreset(const juce::File& file)
{
    if (! file.existsAsFile()) return;
    juce::String error;
    auto state = mu_pp::readFullPreset(file, kFullPresetTag, apvts.state.getType(), error);
    if (! state.isValid())
    {
        if (onLoadError) onLoadError(error);
        return;
    }
    applyStateTree(state);
    publishPresetName(file.getFileNameWithoutExtension());   // mu-link mixer display
}

juce::StringArray PluginProcessor::loadCategoryList() const
{
    return mu_pp::readPresetCategories(getPresetsDir(), getFullPresetExtension(), kFullPresetTag);
}

// A layer preset: the layer's params (prefix-free ids, so it loads into any layer) + its modulators.
void PluginProcessor::saveLayerPreset(int layer, const juce::String& name)
{
    if (layer < 0 || layer >= kNumChannels) return;
    auto dir = getPerSlotPresetDir();
    dir.createDirectory();

    juce::XmlElement root(kLayerPresetTag);
    mu_pp::writeLayerParams(root, *this, layerPrefix(layer));
    if (auto mods = mu_pp::serialiseModulators(voiceSlots[(size_t) layer]).createXml())
        root.addChildElement(mods.release());
    root.writeTo(dir.getChildFile(mu_pp::safePresetFileName(name, "Layer") + "." + getPerSlotPresetExtension()));
}

void PluginProcessor::loadLayerPreset(int layer, const juce::File& file)
{
    if (layer < 0 || layer >= kNumChannels || ! file.existsAsFile()) return;
    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr || ! xml->hasTagName(kLayerPresetTag))
    {
        if (onLoadError) onLoadError("Could not read \"" + file.getFileName() + "\"");
        return;
    }
    const auto tree = juce::ValueTree::fromXml(*xml);
    mu_pp::applyLayerParams(tree, apvts, layerPrefix(layer));

    auto& slot = voiceSlots[(size_t) layer];
    mu_pp::clearModulators(slot);
    mu_pp::deserialiseModulators(tree.getChildWithName("Modulators"), slot, {},
                                 [](const std::string& id) { return mu_toni::isValidModDest(id); });
}

// Reset a layer: its params back to their defaults and its modulators cleared.
void PluginProcessor::resetLayer(int layer)
{
    if (layer < 0 || layer >= kNumChannels) return;
    mu_pp::applyLayerParams({}, apvts, layerPrefix(layer));
    mu_pp::clearModulators(voiceSlots[(size_t) layer]);
}

juce::File PluginProcessor::getPresetsDir()       const { return getContentDir().getChildFile("Presets"); }
juce::File PluginProcessor::getPerSlotPresetDir() const { return getContentDir().getChildFile("Arps"); }

} // namespace mu_toni
