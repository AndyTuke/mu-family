#include "MidiFullPresetMap.h"

void MidiFullPresetMap::setStorageFile(juce::File f)
{
    storageFile = std::move(f);
}

juce::String MidiFullPresetMap::getPresetPath(int index) const
{
    if (index < 0 || index >= NumSlots) return {};
    return paths[(size_t) index];
}

void MidiFullPresetMap::setPresetPath(int index, const juce::File& f)
{
    if (index < 0 || index >= NumSlots) return;
    paths[(size_t) index] = f.getFullPathName();
    save();
}

void MidiFullPresetMap::clearPreset(int index)
{
    if (index < 0 || index >= NumSlots) return;
    paths[(size_t) index].clear();
    save();
}

bool MidiFullPresetMap::hasPreset(int index) const
{
    if (index < 0 || index >= NumSlots) return false;
    return paths[(size_t) index].isNotEmpty();
}

void MidiFullPresetMap::setEnabled(bool e)
{
    enabled.store(e, std::memory_order_relaxed);
    save();
}

void MidiFullPresetMap::load()
{
    if (storageFile == juce::File() || ! storageFile.existsAsFile()) return;

    const juce::var json = juce::JSON::parse(storageFile);
    auto* obj = json.getDynamicObject();
    if (obj == nullptr) return;

    const auto& props = obj->getProperties();

    if (auto* en = props.getVarPointer("enabled"))
        enabled.store((bool) *en, std::memory_order_relaxed);

    if (auto* presetsVar = props.getVarPointer("presets"))
        if (auto* arr = presetsVar->getArray())
            for (int i = 0; i < juce::jmin(NumSlots, arr->size()); ++i)
                paths[(size_t) i] = (*arr)[i].toString();
}

void MidiFullPresetMap::save() const
{
    if (storageFile == juce::File()) return;   // never call before setStorageFile()
    storageFile.getParentDirectory().createDirectory();

    auto* obj = new juce::DynamicObject();
    juce::var json(obj);  // var takes ownership

    obj->setProperty("version", 1);
    obj->setProperty("enabled", enabled.load(std::memory_order_relaxed));

    juce::Array<juce::var> presets;
    presets.ensureStorageAllocated(NumSlots);
    for (int i = 0; i < NumSlots; ++i)
        presets.add(paths[(size_t) i]);
    obj->setProperty("presets", presets);

    storageFile.replaceWithText(juce::JSON::toString(json, true));
}
