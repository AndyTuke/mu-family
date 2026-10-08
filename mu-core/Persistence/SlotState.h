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
// SlotExtras pair. Older full states (the APVTS dump + <VoiceData>) are rebuilt in this shape by
// composeLegacyState before they're applied, so every load runs one apply path.
namespace mu_pp
{

inline const juce::Identifier kGlobalsTag { "Globals" };
inline const juce::Identifier kSlotsTag   { "Slots" };
inline const juce::Identifier kSlotTag    { "Slot" };
inline const juce::Identifier kRowTag     { "p" };
constexpr int kComposedStateFormat = 2;

// A product's non-parameter slot data: `write` adds it to a slot node; `apply` restores it from
// one — clearing whatever an absent child stands for, so a sparse or older node never leaves the
// previous slot's data behind.
struct SlotExtras
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
    if (row.hasProperty("x"))
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

// The product's split of its parameters into slots (by id prefix) and globals (the rest), built once
// after the parameters exist. Message thread only.
class SlotLayout
{
public:
    SlotLayout() = default;

    SlotLayout(juce::AudioProcessor& proc, const juce::StringArray& slotPrefixes)
        : prefixes(slotPrefixes), slots((size_t) slotPrefixes.size())
    {
        // Sort every ranged parameter into its slot's group, or the globals.
        for (auto* raw : proc.getParameters())
            if (auto* p = dynamic_cast<juce::RangedAudioParameter*>(raw))
            {
                const auto id = p->getParameterID();
                const int s = slotOf(id);
                if (s < 0) globals.push_back({ p, id });
                else       slots[(size_t) s].push_back({ p, id.substring(prefixes[s].length()) });
            }
    }

    int numSlots() const noexcept { return prefixes.size(); }
    int numParams(int slot) const { return (int) group(slot).size(); }

    // The slot whose prefix `id` carries (-1 = a global), and `id` without it.
    int slotOf(const juce::String& id) const
    {
        for (int s = 0; s < prefixes.size(); ++s)
            if (id.startsWith(prefixes[s])) return s;
        return -1;
    }
    juce::String rowIdFor(const juce::String& id, int slot) const
    {
        return slot < 0 ? id : id.substring(prefixes[slot].length());
    }

    // A row per parameter of `slot` (-1 = globals), appended to `node`.
    void writeParams(juce::ValueTree& node, int slot) const
    {
        for (const auto& e : group(slot))
        {
            juce::ValueTree row(kRowTag);
            row.setProperty("id", e.rowId, nullptr);
            row.setProperty("x", e.param->convertFrom0to1(e.param->getValue()), nullptr);
            if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(e.param))
                row.setProperty("c", choice->getCurrentChoiceName(), nullptr);
            node.appendChild(row, nullptr);
        }
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
    const std::vector<Entry>& group(int slot) const
    {
        return slot < 0 ? globals : slots[(size_t) juce::jlimit(0, (int) slots.size() - 1, slot)];
    }

    juce::StringArray                prefixes;
    std::vector<std::vector<Entry>>  slots;
    std::vector<Entry>               globals;
};

// One slot as a node of `type` (the product's layer-preset tag, or kSlotTag inside a state).
inline juce::ValueTree captureSlot(const SlotLayout& layout, const SlotExtras& extras, int slot,
                                   const juce::Identifier& type)
{
    juce::ValueTree node(type);
    layout.writeParams(node, slot);
    if (extras.write) extras.write(slot, node);
    return node;
}

inline void applySlot(const SlotLayout& layout, const SlotExtras& extras, int slot, const juce::ValueTree& node)
{
    layout.applyParams(node, slot);
    if (extras.apply) extras.apply(slot, node);
}

inline bool isComposedState(const juce::ValueTree& state)
{
    return (int) state.getProperty("format", 0) >= kComposedStateFormat;
}

// Slot `slot`'s node in a composed state (invalid when it has none).
inline juce::ValueTree findSlotNode(const juce::ValueTree& state, int slot)
{
    const auto slots = state.getChildWithName(kSlotsTag);
    for (int i = 0; i < slots.getNumChildren(); ++i)
        if (const auto node = slots.getChild(i); node.hasType(kSlotTag) && (int) node.getProperty("idx", -1) == slot)
            return node;
    return {};
}

// The globals + every slot as a composed state of `stateType`; the product adds its root properties.
inline juce::ValueTree captureState(const juce::Identifier& stateType, const SlotLayout& layout,
                                    const SlotExtras& extras)
{
    juce::ValueTree state(stateType);
    state.setProperty("format", kComposedStateFormat, nullptr);

    juce::ValueTree globals(kGlobalsTag);
    layout.writeParams(globals, -1);
    state.appendChild(globals, nullptr);

    juce::ValueTree slots(kSlotsTag);
    for (int s = 0; s < layout.numSlots(); ++s)
    {
        auto node = captureSlot(layout, extras, s, kSlotTag);
        node.setProperty("idx", s, nullptr);
        slots.appendChild(node, nullptr);
    }
    state.appendChild(slots, nullptr);
    return state;
}

// Apply a composed state: the globals, then every slot (a slot the state lacks is reset).
inline void applyState(const juce::ValueTree& state, const SlotLayout& layout, const SlotExtras& extras)
{
    layout.applyParams(state.getChildWithName(kGlobalsTag), -1);
    for (int s = 0; s < layout.numSlots(); ++s)
        applySlot(layout, extras, s, findSlotNode(state, s));
}

// A pre-format-2 state — the APVTS dump (<PARAM id value>, actual values) + a <VoiceData> of
// <Voice idx> nodes — rebuilt in the composed shape: each PARAM becomes a row in its slot (prefix
// stripped) or the globals, each Voice node's properties and children move into its slot, and the
// root properties are kept. Every other root child goes to `otherChild(child, composed)` (a
// product's older extras, e.g. a whole-pattern node). A composed state is returned unchanged.
inline juce::ValueTree composeLegacyState(const juce::ValueTree& legacy, const SlotLayout& layout,
                                          const std::function<void(const juce::ValueTree&, juce::ValueTree&)>& otherChild = {})
{
    if (isComposedState(legacy)) return legacy;

    juce::ValueTree state(legacy.getType());
    for (int i = 0; i < legacy.getNumProperties(); ++i)
        state.setProperty(legacy.getPropertyName(i), legacy.getProperty(legacy.getPropertyName(i)), nullptr);
    state.setProperty("format", kComposedStateFormat, nullptr);

    juce::ValueTree globals(kGlobalsTag), slots(kSlotsTag);
    std::vector<juce::ValueTree> slotNodes;
    for (int s = 0; s < layout.numSlots(); ++s)
    {
        juce::ValueTree node(kSlotTag);
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
            const auto id = child.getProperty("id").toString();
            const int s = layout.slotOf(id);
            juce::ValueTree row(kRowTag);
            row.setProperty("id", layout.rowIdFor(id, s), nullptr);
            row.setProperty("x", child.getProperty("value"), nullptr);
            (s < 0 ? globals : slotNodes[(size_t) s]).appendChild(row, nullptr);
        }
        else if (child.hasType(kChannelDataTag))
        {
            for (int v = 0; v < child.getNumChildren(); ++v)
            {
                const auto voice = child.getChild(v);
                const int s = (int) voice.getProperty("idx", -1);
                if (! voice.hasType(kChannelNodeTag) || s < 0 || s >= layout.numSlots()) continue;
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
