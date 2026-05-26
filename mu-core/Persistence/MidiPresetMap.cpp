#include "MidiPresetMap.h"

void MidiPresetMap::setStorageFile(juce::File f)
{
    storageFile = std::move(f);
}

juce::String MidiPresetMap::getPresetPath(int index) const
{
    if (index < 0 || index >= NumSlots) return {};
    return paths[(size_t) index];
}

void MidiPresetMap::setPresetPath(int index, const juce::File& f)
{
    if (index < 0 || index >= NumSlots) return;
    paths[(size_t) index] = f.getFullPathName();
    save();
}

void MidiPresetMap::clearPreset(int index)
{
    if (index < 0 || index >= NumSlots) return;
    paths[(size_t) index].clear();
    save();
}

bool MidiPresetMap::hasPreset(int index) const
{
    if (index < 0 || index >= NumSlots) return false;
    return paths[(size_t) index].isNotEmpty();
}

void MidiPresetMap::setChannelMask(uint8_t mask)
{
    channelMask.store(mask, std::memory_order_relaxed);
    save();
}

void MidiPresetMap::load()
{
    if (storageFile == juce::File() || ! storageFile.existsAsFile()) return;

    const juce::var json = juce::JSON::parse(storageFile);
    auto* obj = json.getDynamicObject();
    if (obj == nullptr) return;

    const auto& props = obj->getProperties();

    if (auto* mask = props.getVarPointer("channelMask"))
        channelMask.store((uint8_t) juce::jlimit(0, 255, (int) *mask),
                          std::memory_order_relaxed);

    if (auto* presetsVar = props.getVarPointer("presets"))
        if (auto* arr = presetsVar->getArray())
            for (int i = 0; i < juce::jmin(NumSlots, arr->size()); ++i)
                paths[(size_t) i] = (*arr)[i].toString();
}

void MidiPresetMap::save() const
{
    if (storageFile == juce::File()) return;   // never call before setStorageFile()
    storageFile.getParentDirectory().createDirectory();

    auto* obj = new juce::DynamicObject();
    juce::var json(obj);  // var takes ownership

    obj->setProperty("version", 1);
    obj->setProperty("channelMask", (int) channelMask.load(std::memory_order_relaxed));

    juce::Array<juce::var> presets;
    presets.ensureStorageAllocated(NumSlots);
    for (int i = 0; i < NumSlots; ++i)
        presets.add(paths[(size_t) i]);
    obj->setProperty("presets", presets);

    storageFile.replaceWithText(juce::JSON::toString(json, true));
}
