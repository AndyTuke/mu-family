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
                         int defaultIdx)
{
    if (! tree.hasProperty(propId))
        return defaultIdx;

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
inline juce::ValueTree serialiseModulators(const VoiceSlot& slot)
{
    juce::ValueTree mods("Modulators");

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

} // namespace mu_pp
