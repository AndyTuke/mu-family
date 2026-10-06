#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <functional>
#include <string>
#include <thread>
#include "Sequencer/VoiceSlot.h"
#include "Modulation/ModulationAssignment.h"
#include "Audio/AlgorithmNames.h"

// Generic modulator serialisation — the per-slot ControlSequences +
// ModulationMatrix assignments are mu-core types, so the (de)serialise lives in
// mu-core and every product shares it (no duplication). It operates on the
// shared VoiceSlot base; a product passes its own source/destination ID
// validators (mu-clid → ModDest::, mu-tant → its kModDestTable) so invalid
// assignments are dropped per-product. Pass empty validators to skip the check
// (the ModulationMatrix still rejects cycles / overflow on add).

namespace mu_pp {

// Optional per-product validator for a modulation source / destination ID.
using ModIdValidator = std::function<bool(const std::string&)>;

// Write an enum value as its stable name string; falls back to the integer if
// the index is out of range.
inline juce::var enumName(const char* const* table, int idx)
{
    if (const char* n = mu_audio::nameFromIndex(table, idx))
        return juce::var(juce::String(n));
    return juce::var(idx);
}

// Read an enum property as int whether it was written as a name string or a
// legacy integer index.
inline int readEnumIndex(const juce::ValueTree& tree,
                         const juce::Identifier& propId,
                         const char* const* nameTable,
                         int defaultIndex)
{
    if (! tree.hasProperty(propId))
        return defaultIndex;

    const auto v = tree.getProperty(propId);
    if (v.isString())
    {
        const int idx = mu_audio::indexFromName(nameTable, v.toString());
        if (idx >= 0)
            return idx;
    }
    return (int) v;
}

// Serialise all ControlSequences + ModulationMatrix assignments for `slot` into
// a <Modulators> ValueTree subtree.
// Marks modulator data saved under the family depth standard (depth = % of the target knob's
// range, Modulation/ModTarget.h). Data without it predates the standard; a product whose old
// depth units differed upgrades it on load (mu-Clid's pad targets).
inline constexpr const char* kDepthUnitsProperty = "depthUnits";
inline constexpr const char* kDepthUnitsRange    = "range";

inline juce::ValueTree serialiseModulators(const VoiceSlot& slot)
{
    juce::ValueTree mods("Modulators");
    mods.setProperty(kDepthUnitsProperty, kDepthUnitsRange, nullptr);

    for (const auto& cs : slot.controlSequences)
    {
        juce::ValueTree seq("Seq");
        seq.setProperty("id",       juce::String(cs.id),                                               nullptr);
        seq.setProperty("mode",     enumName(mu_audio::kModulatorModeNames,     (int)cs.mode),          nullptr);
        seq.setProperty("polarity", enumName(mu_audio::kModulatorPolarityNames, (int)cs.polarity),      nullptr);
        seq.setProperty("loopNV",   enumName(mu_audio::kNoteValueNames,         (int)cs.loopNoteValue), nullptr);
        seq.setProperty("loopMod",  enumName(mu_audio::kNoteModNames,           (int)cs.loopNoteMod),   nullptr);
        seq.setProperty("loopMult", cs.loopMultiplier,                                                  nullptr);
        seq.setProperty("stepNV",   enumName(mu_audio::kNoteValueNames,         (int)cs.stepNoteValue), nullptr);
        seq.setProperty("stepMod",  enumName(mu_audio::kNoteModNames,           (int)cs.stepNoteMod),   nullptr);
        seq.setProperty("stepMult", cs.stepMultiplier,                                                  nullptr);

        for (const auto v : cs.stepValues)
        {
            juce::ValueTree st("Step");
            st.setProperty("v", v, nullptr);
            seq.addChild(st, -1, nullptr);
        }
        for (const auto& pt : cs.curvePoints)
        {
            juce::ValueTree p("Point");
            p.setProperty("x",   pt.x,                          nullptr);
            p.setProperty("y",   pt.y,                          nullptr);
            p.setProperty("bez", pt.hasBezierHandle ? 1 : 0,    nullptr);
            p.setProperty("hx",  pt.handleX,                    nullptr);
            p.setProperty("hy",  pt.handleY,                    nullptr);
            seq.addChild(p, -1, nullptr);
        }

        mods.addChild(seq, -1, nullptr);
    }

    for (const auto& a : slot.modulationMatrix.getAssignments())
    {
        juce::ValueTree asgn("Asgn");
        asgn.setProperty("id",    juce::String(a.id),            nullptr);
        asgn.setProperty("src",   juce::String(a.sourceId),      nullptr);
        asgn.setProperty("dest",  juce::String(a.destinationId), nullptr);
        asgn.setProperty("depth", a.depth,                       nullptr);
        asgn.setProperty("curve", a.curve,                       nullptr);
        mods.addChild(asgn, -1, nullptr);
    }

    return mods;
}

// Deserialise a <Modulators> ValueTree into `slot`. `isValidSource`/`isValidDest`
// (if set) gate each assignment; empty validators skip the check. Returns a list
// of dropped-assignment descriptions (empty on success).
inline juce::StringArray deserialiseModulators(const juce::ValueTree& mods, VoiceSlot& slot,
                                               const ModIdValidator& isValidSource = {},
                                               const ModIdValidator& isValidDest   = {})
{
    juce::StringArray dropped;

    if (!mods.isValid() || mods.getType() != juce::Identifier("Modulators"))
        return dropped;

    while (slot.modLock.exchange(true, std::memory_order_acquire))
        std::this_thread::yield();

    for (int ci = 0; ci < mods.getNumChildren(); ++ci)
    {
        auto node = mods.getChild(ci);

        if (node.getType() == juce::Identifier("Seq"))
        {
            const juce::String id = node.getProperty("id").toString();
            for (auto& cs : slot.controlSequences)
            {
                if (juce::String(cs.id) != id) continue;
                cs.mode          = (ControlSequence::Mode)     readEnumIndex(node, "mode",     mu_audio::kModulatorModeNames,     (int)cs.mode);
                cs.polarity      = (ControlSequence::Polarity) readEnumIndex(node, "polarity", mu_audio::kModulatorPolarityNames, (int)cs.polarity);
                cs.loopNoteValue = (NoteValue)                 readEnumIndex(node, "loopNV",   mu_audio::kNoteValueNames,         (int)cs.loopNoteValue);
                cs.loopNoteMod   = (NoteMod)                   readEnumIndex(node, "loopMod",  mu_audio::kNoteModNames,           (int)cs.loopNoteMod);
                cs.loopMultiplier =                            (int)node.getProperty("loopMult", cs.loopMultiplier);
                cs.stepNoteValue = (NoteValue)                 readEnumIndex(node, "stepNV",   mu_audio::kNoteValueNames,         (int)cs.stepNoteValue);
                cs.stepNoteMod   = (NoteMod)                   readEnumIndex(node, "stepMod",  mu_audio::kNoteModNames,           (int)cs.stepNoteMod);
                cs.stepMultiplier =                            (int)node.getProperty("stepMult", cs.stepMultiplier);

                cs.stepValues.clear();
                cs.curvePoints.clear();
                for (int j = 0; j < node.getNumChildren(); ++j)
                {
                    auto child = node.getChild(j);
                    if (child.getType() == juce::Identifier("Step"))
                    {
                        cs.stepValues.push_back((float)(double)child.getProperty("v", 0.0));
                    }
                    else if (child.getType() == juce::Identifier("Point"))
                    {
                        ControlSequence::CurvePoint pt;
                        pt.x               = (float)(double)child.getProperty("x", 0.0);
                        pt.y               = (float)(double)child.getProperty("y", 0.0);
                        pt.hasBezierHandle = (int)child.getProperty("bez", 0) != 0;
                        pt.handleX         = (float)(double)child.getProperty("hx", 0.0);
                        pt.handleY         = (float)(double)child.getProperty("hy", 0.0);
                        cs.curvePoints.push_back(pt);
                    }
                }

                // Self-heal: a mode/data mismatch — a preset with mode="Smooth" but
                // only <Step>s (or mode="Stepped" with only <Point>s) — would load
                // with the active mode's array empty, so evaluate() outputs a
                // constant 0: modulation that looks wired but is silently inert.
                // Flip the mode to match the data actually present. Both-empty is a
                // legitimately undrawn LFO, so it's left alone.
                if (cs.mode == ControlSequence::Mode::Smooth
                        && cs.curvePoints.empty() && ! cs.stepValues.empty())
                {
                    cs.mode = ControlSequence::Mode::Stepped;
                }
                else if (cs.mode == ControlSequence::Mode::Stepped
                        && cs.stepValues.empty() && ! cs.curvePoints.empty())
                {
                    cs.mode = ControlSequence::Mode::Smooth;
                }

                break;
            }
        }
        else if (node.getType() == juce::Identifier("Asgn"))
        {
            ModulationAssignment a;
            a.id            = node.getProperty("id").toString().toStdString();
            a.sourceId      = node.getProperty("src").toString().toStdString();
            a.destinationId = node.getProperty("dest").toString().toStdString();
            a.depth         = (float)(double)node.getProperty("depth", 0.0);
            a.curve         = (float)(double)node.getProperty("curve", 0.0);

            if (isValidSource && ! isValidSource(a.sourceId))
            {
                dropped.add("invalid source '" + juce::String(a.sourceId) + "'");
                continue;
            }
            if (isValidDest && ! isValidDest(a.destinationId))
            {
                dropped.add("invalid destination '" + juce::String(a.destinationId) + "'");
                continue;
            }

            if (! slot.modulationMatrix.addAssignment(a))
                dropped.add("matrix rejected '" + juce::String(a.destinationId) + "' (cycle or full)");
        }
    }

    slot.modLock.store(false, std::memory_order_release);
    return dropped;
}

// Clear all CS step/curve data + matrix assignments before a deserialise so
// successive preset loads don't accumulate state.
inline void clearModulators(VoiceSlot& slot)
{
    while (slot.modLock.exchange(true, std::memory_order_acquire))
        std::this_thread::yield();

    while (!slot.modulationMatrix.getAssignments().empty())
        slot.modulationMatrix.removeAssignment(slot.modulationMatrix.getAssignments().front().id);

    for (auto& cs : slot.controlSequences)
    {
        cs.stepValues.clear();
        cs.curvePoints.clear();
    }

    slot.modLock.store(false, std::memory_order_release);
}


// ── Per-channel data in the host state (family standard) ─────────────────────
// Every product embeds its channels' modulators in the APVTS state as
//   <VoiceData> <Voice idx="N"> <Modulators .../> [product extras] </Voice> ... </VoiceData>
// so copyState() carries them into the DAW session and full-preset files. Products with an
// older shape convert it to this one before reading.
inline constexpr const char* kChannelDataTag = "VoiceData";
inline constexpr const char* kChannelNodeTag = "Voice";

// Replace `state`'s <VoiceData> with one <Voice idx> per channel 0..numChannels-1 holding that
// channel's modulators; `addExtras` (optional) appends the product's own data to each node.
template <typename SlotAt>
inline void writeChannelData(juce::ValueTree& state, int numChannels, SlotAt&& slotAt,
                             const std::function<void(int, juce::ValueTree&)>& addExtras = {})
{
    state.removeChild(state.getChildWithName(kChannelDataTag), nullptr);
    juce::ValueTree data(kChannelDataTag);
    for (int ch = 0; ch < numChannels; ++ch)
    {
        juce::ValueTree node(kChannelNodeTag);
        node.setProperty("idx", ch, nullptr);
        node.addChild(serialiseModulators(slotAt(ch)), -1, nullptr);
        if (addExtras) addExtras(ch, node);
        data.addChild(node, -1, nullptr);
    }
    state.addChild(data, -1, nullptr);
}

// Channel `ch`'s <Voice> node in `state`, or an invalid tree if it has none.
inline juce::ValueTree findChannelNode(const juce::ValueTree& state, int ch)
{
    const auto data = state.getChildWithName(kChannelDataTag);
    for (int i = 0; i < data.getNumChildren(); ++i)
    {
        const auto node = data.getChild(i);
        if (node.hasType(kChannelNodeTag) && (int) node.getProperty("idx", -1) == ch)
            return node;
    }
    return {};
}

// Clear each channel's modulators and restore them from its node (a channel with no node
// stays cleared, so an older / foreign state never leaves stale assignments behind).
// `isValidDest(ch, id)` (optional) drops targets the channel doesn't have.
template <typename SlotAt>
inline void readChannelModulators(const juce::ValueTree& state, int numChannels, SlotAt&& slotAt,
                                  const std::function<bool(int, const std::string&)>& isValidDest = {})
{
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto& slot = slotAt(ch);
        clearModulators(slot);
        ModIdValidator destCheck;
        if (isValidDest) destCheck = [&isValidDest, ch](const std::string& id) { return isValidDest(ch, id); };
        deserialiseModulators(findChannelNode(state, ch).getChildWithName("Modulators"), slot, {}, destCheck);
    }
}

} // namespace mu_pp
