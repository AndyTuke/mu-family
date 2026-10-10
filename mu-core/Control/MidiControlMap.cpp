#include "Control/MidiControlMap.h"
#include "Persistence/PresetFiles.h"   // mu_pp::replaceFileAtomically

namespace mu_core
{

MidiControlMap::MidiControlMap()
{
    for (auto& l : lookup) l.store(-1, std::memory_order_relaxed);
}

void MidiControlMap::setStorageFile(juce::File f) { storageFile = std::move(f); }

void MidiControlMap::republish()
{
    epochCounter.fetch_add(1, std::memory_order_acq_rel);   // odd: rewriting
    for (auto& l : lookup) l.store(-1, std::memory_order_relaxed);
    for (auto& f : flags)  f.store(0, std::memory_order_relaxed);
    for (size_t i = 0; i < dirty.size(); ++i)   // every slot, so a stale flag past the new size cannot fire later
    {
        latest[i].store(0.0f, std::memory_order_relaxed);
        dirty[i].store(false, std::memory_order_relaxed);
    }
    for (size_t i = 0; i < mappings.size(); ++i)
    {
        const auto& m = mappings[i];
        lookup[slot(m.isNote, m.channel, m.number)].store((int16_t) i, std::memory_order_relaxed);
        flags[i].store((uint32_t) m.action.type | ((uint32_t) m.action.quantise << 8), std::memory_order_relaxed);
    }
    epochCounter.fetch_add(1, std::memory_order_acq_rel);   // even: settled
}

int MidiControlMap::add(const Mapping& m)
{
    if (m.channel < 1 || m.channel > 16 || m.number < 0 || m.number > 127 || m.action.type == ControlActionType::None)
        return -1;
    int index = -1;
    {
        const juce::ScopedLock sl(lock);
        for (size_t i = 0; i < mappings.size(); ++i)
            if (mappings[i].isNote == m.isNote && mappings[i].channel == m.channel && mappings[i].number == m.number)
            {
                mappings[i] = m;
                index = (int) i;
                break;
            }
        if (index < 0)
        {
            if ((int) mappings.size() >= kMaxMappings) return -1;
            mappings.push_back(m);
            index = (int) mappings.size() - 1;
        }
        republish();
    }
    save();
    return index;
}

void MidiControlMap::remove(int index)
{
    {
        const juce::ScopedLock sl(lock);
        if (index < 0 || index >= (int) mappings.size()) return;
        mappings.erase(mappings.begin() + index);
        republish();
    }
    save();
}

void MidiControlMap::clear()
{
    {
        const juce::ScopedLock sl(lock);
        mappings.clear();
        republish();
    }
    save();
}

int MidiControlMap::size() const
{
    const juce::ScopedLock sl(lock);
    return (int) mappings.size();
}

Mapping MidiControlMap::get(int index) const
{
    const juce::ScopedLock sl(lock);
    return (index >= 0 && index < (int) mappings.size()) ? mappings[(size_t) index] : Mapping {};
}

void MidiControlMap::setQuantise(Quantise q)
{
    globalQuantise.store((int) (q == Quantise::Default ? Quantise::Bar : q), std::memory_order_relaxed);
    save();
}

bool MidiControlMap::takeLearned(bool& isNote, int& channel, int& number) noexcept
{
    const uint32_t v = learned.exchange(0, std::memory_order_acq_rel);
    if (v == 0) return false;
    isNote  = (v & 2u) != 0;
    channel = (int) ((v >> 8) & 0xFF);
    number  = (int) ((v >> 16) & 0xFF);
    learning.store(false, std::memory_order_release);
    return true;
}

void MidiControlMap::offerLearn(bool isNote, int channel, int number) noexcept
{
    uint32_t expected = 0;
    learned.compare_exchange_strong(expected, 1u | (isNote ? 2u : 0u) | ((uint32_t) channel << 8) | ((uint32_t) number << 16),
                                    std::memory_order_acq_rel);
}

void MidiControlMap::load()
{
    if (storageFile == juce::File() || ! storageFile.existsAsFile()) return;
    const juce::var json = juce::JSON::parse(storageFile);
    auto* obj = json.getDynamicObject();
    if (obj == nullptr) return;

    const juce::ScopedLock sl(lock);
    mappings.clear();

    const auto q = quantiseFromName(obj->getProperty("quantise").toString());
    globalQuantise.store((int) (q == Quantise::Default ? Quantise::Bar : q), std::memory_order_relaxed);

    if (auto* arr = obj->getProperty("mappings").getArray())
        for (const auto& v : *arr)
        {
            Mapping m;
            m.isNote          = v.getProperty("type", "cc").toString() == "note";
            m.channel         = juce::jlimit(1, 16, (int) v.getProperty("channel", 1));
            m.number          = juce::jlimit(0, 127, (int) v.getProperty("number", 0));
            m.action.type     = actionFromName(v.getProperty("action", "").toString());
            m.action.layer    = (int) v.getProperty("layer", 0);
            m.action.paramId  = v.getProperty("param", "").toString();
            m.action.quantise = quantiseFromName(v.getProperty("quantise", "default").toString());
            if (m.action.type != ControlActionType::None && (int) mappings.size() < kMaxMappings)
                mappings.push_back(m);
        }
    republish();
}

void MidiControlMap::save() const
{
    if (storageFile == juce::File()) return;   // never before setStorageFile()
    storageFile.getParentDirectory().createDirectory();

    auto* obj = new juce::DynamicObject();
    juce::var json(obj);
    obj->setProperty("version", 1);
    obj->setProperty("quantise", quantiseName(getQuantise()));

    juce::Array<juce::var> rows;
    {
        const juce::ScopedLock sl(lock);
        for (const auto& m : mappings)
        {
            auto* r = new juce::DynamicObject();
            r->setProperty("type",     m.isNote ? "note" : "cc");
            r->setProperty("channel",  m.channel);
            r->setProperty("number",   m.number);
            r->setProperty("action",   actionName(m.action.type));
            r->setProperty("layer",    m.action.layer);
            r->setProperty("param",    m.action.paramId);
            r->setProperty("quantise", quantiseName(m.action.quantise));
            rows.add(juce::var(r));
        }
    }
    obj->setProperty("mappings", rows);
    mu_pp::replaceFileAtomically(storageFile, juce::JSON::toString(json, true));
}

} // namespace mu_core
