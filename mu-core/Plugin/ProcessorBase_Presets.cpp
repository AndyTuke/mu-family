// ProcessorBase, part of the class split by topic (code moved unchanged from ProcessorBase.cpp).
// Full and typed-layer preset save / load, preset directories and category lists.
#include "Plugin/ProcessorBase.h"
#include "Plugin/MixerFxParams.h"           // mu_mixfx::isGlobalFxParamId
#include "Sequencer/ClipClipboard.h"         // the process-wide clip clipboard
#include "Persistence/PresetFiles.h"         // mu_pp full-preset files
#include "Audio/FX/Slots/FXAlgorithmDef.h"   // FXAlgorithmRegistry
#include "Audio/AlgorithmNames.h"             // mu_audio::kInsertAlgorithmCount
#include <cstring>                            // std::strcmp — alloc-free suffix compare
#include <map>
#include <memory>
#include <mutex>

void ProcessorBase::savePreset(const juce::String& name, const juce::String& desc,
                               const juce::String& category, bool /*embedSamples*/)
{
    if (const char* tag = getFullPresetTag())
        mu_pp::writeFullPreset(getPresetsDir(), getFullPresetExtension(), tag,
                               name, desc, category, captureFullPreset(), onLoadError);
}

bool ProcessorBase::saveFullPresetTo(const juce::File& file)
{
    const char* tag = getFullPresetTag();
    if (tag == nullptr) return false;
    return mu_pp::writeFullPreset(file.getParentDirectory(), getFullPresetExtension(), tag,
                                  file.getFileNameWithoutExtension(), {}, {}, captureFullPreset(), onLoadError)
           != juce::File();
}

juce::File ProcessorBase::getPresetsDir() const
{
    const auto content = getContentDir();
    return content == juce::File() ? juce::File() : content.getChildFile("Presets");
}

juce::Array<juce::File> ProcessorBase::slotPresetFiles(int /*slot*/) const
{
    return mu_pp::listPresetFiles(getLayerPresetDir(), getLayerPresetExtension());
}

// rt-ok-fn: preset/clip handling on the message thread
juce::File ProcessorBase::presetDirOf(const mu_core::LayerTypeInfo& info) const
{
    const auto content = getContentDir();
    if (content == juce::File() || juce::String(info.contentFolderName).isEmpty() || juce::String(info.presetFolder).isEmpty())
        return {};
    return content.getSiblingFile(info.contentFolderName).getChildFile(info.presetFolder);
}

// rt-ok-fn: preset/clip handling on the message thread
juce::String ProcessorBase::presetExtensionOf(const mu_core::LayerTypeInfo& info)
{
    return juce::String(info.presetExtension).trimCharactersAtStart(".");
}

mu_core::LayerTypeId ProcessorBase::slotTypeId(int slot) const
{
    const auto* layer = getLayer(slot);
    return layer != nullptr ? (mu_core::LayerTypeId) layer->typeId() : mu_core::LayerTypeId::Empty;
}

juce::File ProcessorBase::typedLayerPresetDir(int slot) const
{
    const auto* info = layerTypes.find(slotTypeId(slot));
    return info != nullptr ? presetDirOf(*info) : juce::File();
}

juce::String ProcessorBase::typedLayerPresetExtension(int slot) const
{
    const auto* info = layerTypes.find(slotTypeId(slot));
    return info != nullptr ? presetExtensionOf(*info) : juce::String();
}

juce::Array<juce::File> ProcessorBase::typedSlotPresetFiles(int slot) const
{
    juce::Array<juce::File> out;
    auto addType = [&](const mu_core::LayerTypeInfo& info)
    {
        for (const auto& f : mu_pp::listPresetFiles(presetDirOf(info), presetExtensionOf(info)))
        {
            // Types that share an extension: keep the files that name this type (an unreadable file names none).
            if (info.typeFromPreset != nullptr)
            {
                const auto xml = juce::XmlDocument::parse(f);
                if (xml == nullptr || info.typeFromPreset(juce::ValueTree::fromXml(*xml)) != info.id) continue;
            }
            out.addIfNotAlreadyThere(f);
        }
    };
    const auto* own = layerTypes.find(slotTypeId(slot));
    if (own != nullptr)
        addType(*own);
    else
        for (int t = 0; t < layerTypes.size(); ++t) addType(layerTypes.at(t));
    return out;
}

// rt-ok-fn: preset/clip handling on the message thread
mu_core::LayerTypeId ProcessorBase::layerPresetType(const juce::ValueTree& preset, const juce::String& extension) const
{
    auto found = mu_core::LayerTypeId::Empty;
    int matches = 0;
    for (int t = 0; t < layerTypes.size(); ++t)
    {
        const auto& info = layerTypes.at(t);
        if (extension.isNotEmpty() && ! presetExtensionOf(info).equalsIgnoreCase(extension.trimCharactersAtStart("."))) continue;
        if (juce::String(info.presetTag).isNotEmpty() && ! preset.hasType(info.presetTag)) continue;
        if (info.typeFromPreset != nullptr && info.typeFromPreset(preset) != info.id) continue;
        found = info.id;
        ++matches;
    }
    return matches == 1 ? found : mu_core::LayerTypeId::Empty;
}

// rt-ok-fn: preset/clip handling on the message thread
void ProcessorBase::saveTypedLayerPreset(int slot, const juce::String& name)
{
    const auto* info = layerTypes.find(slotTypeId(slot));
    if (info == nullptr || juce::String(info->presetTag).isEmpty()) return;   // an Empty slot has nothing to save
    const auto dir = presetDirOf(*info);
    if (dir == juce::File()) return;
    const auto node = captureLayerNode(slot, info->presetTag);
    // A type whose file keeps another shape than the composed node converts it at the file boundary.
    const auto fileTree = info->presetNodeToFile != nullptr ? info->presetNodeToFile(node, name) : node;
    if (auto xml = fileTree.createXml())
        mu_pp::writeXmlAtomically(*xml, dir.getChildFile(mu_pp::safePresetFileName(name, info->name) + "." + presetExtensionOf(*info)),
                                  onLoadError);
}

void ProcessorBase::loadTypedLayerPreset(int slot, const juce::File& file)
{
    if (getLayer(slot) == nullptr || ! file.existsAsFile()) return;
    const auto xml = juce::XmlDocument::parse(file);
    const auto tree = xml != nullptr ? juce::ValueTree::fromXml(*xml) : juce::ValueTree();
    const auto type = tree.isValid() ? layerPresetType(tree, file.getFileExtension()) : mu_core::LayerTypeId::Empty;
    const auto* fileInfo = layerTypes.find(type);
    if (fileInfo == nullptr)
    {
        if (onLoadError) onLoadError("Could not read \"" + file.getFileName() + "\"");
        return;
    }

    // The composed node the slot applies: the file itself, or its conversion for a type with another file shape.
    const auto node = fileInfo->presetFileToNode != nullptr ? fileInfo->presetFileToNode(tree, file.getFileNameWithoutExtension()) : tree;
    if (! node.isValid())
    {
        if (onLoadError) onLoadError("Could not read \"" + file.getFileName() + "\"");
        return;
    }

    const auto slotType = slotTypeId(slot);
    if (slotType == mu_core::LayerTypeId::Empty)
    {
        // An Empty slot takes the file's type, with the file's rows as the new layer's first state.
        if (setLayerType(slot, type, &node) && onLayerPresetCommitted)
            onLayerPresetCommitted(slot);
        return;
    }
    if (slotType != type)
    {
        // A type changes only through the chooser or the header menu, never as a side effect of a preset.
        const auto* slotInfo = layerTypes.find(slotType);
        if (onLoadError)
        {
            // "a Kick", "an Arp": the article follows the type name's first letter.
            auto withArticle = [](const juce::String& name) { return (name.indexOfAnyOf("AEIOUaeiou") == 0 ? "an " : "a ") + name; };
            onLoadError("This is " + withArticle(fileInfo->name) + " preset; this slot is "
                        + withArticle(slotInfo != nullptr ? juce::String(slotInfo->name) : juce::String("layer")));
        }
        return;
    }
    stageLayerPreset(slot, node, false, false);
}

void ProcessorBase::applyStagedTypedLayer(int slot, const juce::ValueTree& tree)
{
    const bool sameType = layerPresetType(tree) == slotTypeId(slot);
    jassert(sameType);   // a type change cancels the slot's stage, so a stage of another type cannot reach here
    if (! sameType) return;
    applyLayerNode(slot, tree);
    if (onLayerPresetCommitted) onLayerPresetCommitted(slot);
}

juce::File ProcessorBase::getDefaultPresetFile() const
{
    return getPresetsDir().getChildFile("_default." + getFullPresetExtension());
}

void ProcessorBase::loadDefaultPreset()
{
    const auto f = getDefaultPresetFile();
    if (f.existsAsFile())
        loadPreset(f);
}

void ProcessorBase::loadPreset(const juce::File& file)
{
    const char* tag = getFullPresetTag();
    if (tag == nullptr || ! file.existsAsFile()) { noteFullPresetLoadFailed(); return; }
    juce::String error;
    auto state = mu_pp::readFullPreset(file, tag, apvts.state.getType(), error);
    if (! state.isValid())
    {
        noteFullPresetLoadFailed();
        if (onLoadError) onLoadError(error);
        return;
    }
    useLoadedFullPreset(std::move(state));
    publishPresetName(file.getFileNameWithoutExtension());   // mu-link mixer display
}

juce::StringArray ProcessorBase::loadCategoryList() const
{
    const char* tag = getFullPresetTag();
    return tag != nullptr ? mu_pp::readPresetCategories(getPresetsDir(), getFullPresetExtension(), tag)
                          : juce::StringArray();
}
