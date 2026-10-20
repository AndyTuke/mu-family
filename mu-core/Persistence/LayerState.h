#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Modulation/ModulatorSerialise.h"   // kChannelDataTag / kChannelNodeTag (the pre-format-2 shape)
#include <cmath>
#include <functional>
#include <map>
#include <vector>

// The family's composed state (format 2): a slot — one rhythm / voice / layer / lane — is ONE unit,
// saved and applied by the same code wherever it appears.
//
//   layer preset         <ProductLayerTag> rows + extras </ProductLayerTag>
//   full preset / host   <ProductState format="2" …product root properties…>
//                          <Globals> rows for every parameter outside the slots </Globals>
//                          <Slots> <Slot idx="0"> rows + extras </Slot> … </Slots>
//                        </ProductState>
//
// A row is <p id="…" x="…"/>: the id without the slot's prefix (so a slot loads into any slot) and
// the ACTUAL value, plus c="…" (the choice name) for a choice parameter — a range or choice-list
// change can't silently shift a saved value. Rows written before format 2 (normalised v="…") still
// read. A product's non-parameter slot data (modulators, gates, step rows, samples) is its
// Layer::writeExtras / applyExtras (carried here as a LayerExtras pair). Older full states (the APVTS dump + <VoiceData>) are rebuilt in this shape by
// composeLegacyState before they're applied, so every load runs one apply path.
namespace mu_pp
{

inline const juce::Identifier kGlobalsTag { "Globals" };
inline const juce::Identifier kLayersTag   { "Slots" };
inline const juce::Identifier kLayerTag    { "Slot" };
inline const juce::Identifier kRowTag     { "p" };
constexpr int kComposedStateFormat = 2;

// A product's non-parameter slot data: `write` adds it to a slot node; `apply` restores it from
// one — clearing whatever an absent child stands for, so a sparse or older node never leaves the
// previous slot's data behind.
struct LayerExtras
{
    std::function<void(int slot, juce::ValueTree& node)>       write;
    std::function<void(int slot, const juce::ValueTree& node)> apply;
};

// The normalised value `row` holds for `p` — by choice name (c), else actual value (x), else the
// pre-format-2 normalised value (v). False when the row holds nothing usable.
inline bool readRowValue(const juce::ValueTree& row, juce::RangedAudioParameter& p, float& normalised)
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(&p); choice != nullptr && row.hasProperty("c"))
    {
        const int idx = choice->choices.indexOf(row.getProperty("c").toString());
        if (idx >= 0) { normalised = p.convertTo0to1((float) idx); return true; }
    }
    if (row.hasProperty("x") && ! row.getProperty("x").isVoid())
    {
        const auto x = (float) (double) row.getProperty("x");
        if (! std::isfinite(x)) return false;
        normalised = p.convertTo0to1(x);
        return true;
    }
    if (row.hasProperty("v"))
    {
        const auto v = (float) (double) row.getProperty("v");
        if (! std::isfinite(v)) return false;
        normalised = juce::jlimit(0.0f, 1.0f, v);
        return true;
    }
    return false;
}

// A row for `p` under `rowId`, appended to `node`: its actual value, plus its choice name.
inline void appendParamRow(juce::ValueTree& node, juce::RangedAudioParameter& p, const juce::String& rowId)
{
    juce::ValueTree row(kRowTag);
    row.setProperty("id", rowId, nullptr);
    row.setProperty("x", p.convertFrom0to1(p.getValue()), nullptr);
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(&p))
        row.setProperty("c", choice->getCurrentChoiceName(), nullptr);
    node.appendChild(row, nullptr);
}

// The product's split of its parameters into slots (by id prefix) and globals (the rest), built once
// after the parameters exist. Message thread only.
//
// A slot normally has ONE prefix (the four list products). A slot host whose slot can hold different layer
// types gives each slot several prefixes, one per type that has its own parameter rows. Exactly one of them
// is LIVE at a time (the host names it with setLivePrefix); only the live prefix's rows are written, applied,
// copied or reset, and the dormant ones keep their defaults and never reach a saved file.
class LayerLayout
{
public:
    LayerLayout() = default;

    // One prefix per slot: slot i owns the ids starting with layerPrefixes[i], always live.
    LayerLayout(juce::AudioProcessor& proc, const juce::StringArray& layerPrefixes)
        : LayerLayout(proc, toSets(layerPrefixes), 0)
    {
    }

    // Several prefixes per slot: prefixSets[i] are the prefixes slot i's parameters may carry. A slot with a
    // single prefix is live on it; a slot with several has none live until setLivePrefix names one.
    static LayerLayout withPrefixSets(juce::AudioProcessor& proc, const std::vector<juce::StringArray>& prefixSets)
    {
        return LayerLayout(proc, prefixSets, 0);
    }

    // Earlier parameter-id prefixes of the slots, in slot order (entry i belongs to slot i). Only
    // composeLegacyState consults them, for ids the current prefixes do not match; live ids never do.
    void addLegacyPrefixes(const juce::StringArray& oldPrefixes) { legacyPrefixes = oldPrefixes; }

    // The slot whose earlier prefix `id` carries (-1 = none), and `id` without that prefix.
    int legacySlotOf(const juce::String& id) const
    {
        for (int s = 0; s < legacyPrefixes.size() && s < numLayers(); ++s)
            if (legacyPrefixes[s].isNotEmpty() && id.startsWith(legacyPrefixes[s])) return s;
        return -1;
    }
    juce::String legacyRowIdFor(const juce::String& id, int slot) const
    {
        return id.substring(legacyPrefixes[slot].length());
    }

    int numLayers() const noexcept { return (int) prefixSets.size(); }
    // The LIVE parameter-id prefix of `slot`: empty when the layout has no such slot or the slot has no live
    // prefix (an Empty slot of a typed host). The prefix every per-slot parameter operation works on.
    juce::String prefix(int slot) const
    {
        if (slot < 0 || slot >= numLayers()) return {};
        const int l = live[(size_t) slot];
        return l >= 0 ? prefixSets[(size_t) slot][l] : juce::String();
    }
    int numParams(int slot) const { return (int) group(slot).size(); }

    // Make `prefix` the slot's live one (it must be one of the slot's prefixes); none is live when it is empty
    // or not the slot's. A slot with a lone prefix stays live on it whatever is asked. Message thread; a typed
    // host calls it as the slot's layer type changes.
    void setLivePrefix(int slot, const juce::String& prefix)
    {
        if (slot < 0 || slot >= numLayers() || prefixSets[(size_t) slot].size() == 1) return;
        live[(size_t) slot] = prefix.isEmpty() ? -1 : prefixSets[(size_t) slot].indexOf(prefix);
    }

    // The slot whose prefix `id` carries (-1 = a global), and `id` without it. Any of a slot's prefixes
    // matches, live or not, so a dormant parameter still belongs to its slot rather than the globals.
    int slotOf(const juce::String& id) const
    {
        for (int s = 0; s < numLayers(); ++s)
            if (matchingPrefix(id, s) >= 0) return s;
        return -1;
    }
    juce::String rowIdFor(const juce::String& id, int slot) const
    {
        if (slot < 0) return id;
        const int k = matchingPrefix(id, slot);
        return k >= 0 ? id.substring(prefixSets[(size_t) slot][k].length()) : id;
    }

    // A row per parameter of `slot` (-1 = globals), appended to `node`.
    void writeParams(juce::ValueTree& node, int slot) const
    {
        for (const auto& e : group(slot))
            appendParamRow(node, *e.param, e.rowId);
    }

    // `node`'s rows onto `slot` (-1 = globals). Each parameter is written once — to its row's value,
    // or its default when the node has no row for it — and not at all when it already holds it.
    void applyParams(const juce::ValueTree& node, int slot) const
    {
        std::map<juce::String, juce::ValueTree> rows;
        for (int i = 0; i < node.getNumChildren(); ++i)
            if (const auto row = node.getChild(i); row.hasType(kRowTag))
                rows[row.getProperty("id").toString()] = row;

        for (const auto& e : group(slot))
        {
            float target = e.param->getDefaultValue();
            if (const auto it = rows.find(e.rowId); it != rows.end())
                readRowValue(it->second, *e.param, target);
            if (e.param->getValue() != target)
                e.param->setValueNotifyingHost(target);
        }
    }

private:
    struct Entry { juce::RangedAudioParameter* param; juce::String rowId; };

    static std::vector<juce::StringArray> toSets(const juce::StringArray& onePerSlot)
    {
        std::vector<juce::StringArray> sets;
        for (const auto& p : onePerSlot) sets.push_back(juce::StringArray(p));
        return sets;
    }

    // `unused` only separates this from the public one-prefix-per-slot constructor.
    LayerLayout(juce::AudioProcessor& proc, const std::vector<juce::StringArray>& sets, int unused)
        : prefixSets(sets), live(sets.size(), -1), slots(sets.size())
    {
        juce::ignoreUnused(unused);
        for (size_t s = 0; s < sets.size(); ++s)
        {
            slots[s].resize((size_t) sets[s].size());
            if (sets[s].size() == 1) live[s] = 0;   // a lone prefix is always the live one
        }

        // Sort every ranged parameter into its slot's group (per prefix), or the globals.
        for (auto* raw : proc.getParameters())
            if (auto* p = dynamic_cast<juce::RangedAudioParameter*>(raw))
            {
                const auto id = p->getParameterID();
                const int s = slotOf(id);
                if (s < 0) { globals.push_back({ p, id }); continue; }
                const int k = matchingPrefix(id, s);
                slots[(size_t) s][(size_t) k].push_back({ p, id.substring(prefixSets[(size_t) s][k].length()) });
            }
    }

    // The index of the slot's prefix that `id` starts with (-1 = none).
    int matchingPrefix(const juce::String& id, int slot) const
    {
        const auto& set = prefixSets[(size_t) slot];
        for (int k = 0; k < set.size(); ++k)
            if (id.startsWith(set[k])) return k;
        return -1;
    }

    // The rows of `slot`'s live prefix (-1 = the globals); none for a slot with no live prefix.
    const std::vector<Entry>& group(int slot) const
    {
        if (slot < 0) return globals;
        const auto s = (size_t) juce::jlimit(0, (int) slots.size() - 1, slot);
        return live[s] >= 0 ? slots[s][(size_t) live[s]] : none;
    }

    std::vector<juce::StringArray>               prefixSets;
    std::vector<int>                             live;    // per slot: index into its prefix set, -1 = none live
    juce::StringArray                            legacyPrefixes;
    std::vector<std::vector<std::vector<Entry>>> slots;   // [slot][prefix][row]
    std::vector<Entry>                           globals;
    std::vector<Entry>                           none;
};

// One slot as a node of `type` (the product's layer-preset tag, or kLayerTag inside a state).
inline juce::ValueTree captureLayer(const LayerLayout& layout, const LayerExtras& extras, int slot,
                                   const juce::Identifier& type)
{
    juce::ValueTree node(type);
    layout.writeParams(node, slot);
    if (extras.write) extras.write(slot, node);
    return node;
}

inline void applyLayer(const LayerLayout& layout, const LayerExtras& extras, int slot, const juce::ValueTree& node)
{
    layout.applyParams(node, slot);
    if (extras.apply) extras.apply(slot, node);
}

inline bool isComposedState(const juce::ValueTree& state)
{
    return (int) state.getProperty("format", 0) >= kComposedStateFormat;
}

// Slot `slot`'s node in a composed state (invalid when it has none).
inline juce::ValueTree findLayerNode(const juce::ValueTree& state, int slot)
{
    const auto slots = state.getChildWithName(kLayersTag);
    for (int i = 0; i < slots.getNumChildren(); ++i)
        if (const auto node = slots.getChild(i); node.hasType(kLayerTag) && (int) node.getProperty("idx", -1) == slot)
            return node;
    return {};
}

// The globals + every slot as a composed state of `stateType`; the product adds its root properties.
inline juce::ValueTree captureState(const juce::Identifier& stateType, const LayerLayout& layout,
                                    const LayerExtras& extras)
{
    juce::ValueTree state(stateType);
    state.setProperty("format", kComposedStateFormat, nullptr);

    juce::ValueTree globals(kGlobalsTag);
    layout.writeParams(globals, -1);
    state.appendChild(globals, nullptr);

    juce::ValueTree slots(kLayersTag);
    for (int s = 0; s < layout.numLayers(); ++s)
    {
        auto node = captureLayer(layout, extras, s, kLayerTag);
        node.setProperty("idx", s, nullptr);
        slots.appendChild(node, nullptr);
    }
    state.appendChild(slots, nullptr);
    return state;
}

// Apply a composed state: the globals, then every slot (a slot the state lacks is reset).
inline void applyState(const juce::ValueTree& state, const LayerLayout& layout, const LayerExtras& extras)
{
    layout.applyParams(state.getChildWithName(kGlobalsTag), -1);
    for (int s = 0; s < layout.numLayers(); ++s)
        applyLayer(layout, extras, s, findLayerNode(state, s));
}

// A pre-format-2 state — the APVTS dump (<PARAM id value>, actual values) + a <VoiceData> of
// <Voice idx> nodes — rebuilt in the composed shape: each PARAM becomes a row in its slot (prefix
// stripped) or the globals, each Voice node's properties and children move into its slot, and the
// root properties are kept. Every other root child goes to `otherChild(child, composed)` (a
// product's older extras, e.g. a whole-pattern node). A composed state is returned unchanged.
inline juce::ValueTree composeLegacyState(const juce::ValueTree& legacy, const LayerLayout& layout,
                                          const std::function<void(const juce::ValueTree&, juce::ValueTree&)>& otherChild = {})
{
    if (isComposedState(legacy)) return legacy;

    juce::ValueTree state(legacy.getType());
    for (int i = 0; i < legacy.getNumProperties(); ++i)
        state.setProperty(legacy.getPropertyName(i), legacy.getProperty(legacy.getPropertyName(i)), nullptr);
    state.setProperty("format", kComposedStateFormat, nullptr);

    juce::ValueTree globals(kGlobalsTag), slots(kLayersTag);
    std::vector<juce::ValueTree> slotNodes;
    for (int s = 0; s < layout.numLayers(); ++s)
    {
        juce::ValueTree node(kLayerTag);
        node.setProperty("idx", s, nullptr);
        slotNodes.push_back(node);
        slots.appendChild(node, nullptr);
    }
    state.appendChild(globals, nullptr);
    state.appendChild(slots, nullptr);

    // Sort the old root children: params into rows, per-voice data into slots, the rest to the product.
    for (int i = 0; i < legacy.getNumChildren(); ++i)
    {
        const auto child = legacy.getChild(i);
        if (child.hasType("PARAM"))
        {
            // A PARAM with no value (APVTS leaves these for params it never set) means the
            // default, as replaceState read it — leave it out so the apply defaults it.
            if (! child.hasProperty("value")) continue;
            const auto id = child.getProperty("id").toString();
            int s = layout.slotOf(id);
            auto rowId = layout.rowIdFor(id, s);
            if (s < 0)   // no current prefix matches: try the prefixes the product used before a rename
                if (const int old = layout.legacySlotOf(id); old >= 0)
                {
                    s = old;
                    rowId = layout.legacyRowIdFor(id, old);
                }
            juce::ValueTree row(kRowTag);
            row.setProperty("id", rowId, nullptr);
            row.setProperty("x", child.getProperty("value"), nullptr);
            (s < 0 ? globals : slotNodes[(size_t) s]).appendChild(row, nullptr);
        }
        else if (child.hasType(kChannelDataTag))
        {
            for (int v = 0; v < child.getNumChildren(); ++v)
            {
                const auto voice = child.getChild(v);
                const int s = (int) voice.getProperty("idx", -1);
                if (! voice.hasType(kChannelNodeTag) || s < 0 || s >= layout.numLayers()) continue;
                auto& node = slotNodes[(size_t) s];
                for (int p = 0; p < voice.getNumProperties(); ++p)
                    if (const auto name = voice.getPropertyName(p); name != juce::Identifier("idx"))
                        node.setProperty(name, voice.getProperty(name), nullptr);
                for (int c = 0; c < voice.getNumChildren(); ++c)
                    node.appendChild(voice.getChild(c).createCopy(), nullptr);
            }
        }
        else if (otherChild)
        {
            otherChild(child, state);
        }
    }
    return state;
}

} // namespace mu_pp
