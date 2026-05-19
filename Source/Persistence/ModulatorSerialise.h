#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <thread>
#include "Sequencer/Rhythm.h"
#include "Modulation/ModulationDestinations.h"
#include "Audio/AlgorithmNames.h"

// Modulator serialisation helpers — extracted from PluginProcessor_Preset.cpp
// so both the preset-load path and the test suite can call them without pulling
// in the full PluginProcessor include chain.

namespace mu_pp {

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

// Serialise all ControlSequences + ModulationMatrix assignments for `r` into a
// <Modulators> ValueTree subtree.
inline juce::ValueTree serialiseModulators(const Rhythm& r)
{
    juce::ValueTree mods("Modulators");

    for (const auto& cs : r.controlSequences)
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

    for (const auto& a : r.modulationMatrix.getAssignments())
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

// Deserialise a <Modulators> ValueTree into `r`. Validates source/dest IDs and
// returns a list of any dropped-assignment descriptions (empty on success).
inline juce::StringArray deserialiseModulators(const juce::ValueTree& mods, Rhythm& r)
{
    juce::StringArray dropped;

    if (!mods.isValid() || mods.getType() != juce::Identifier("Modulators"))
        return dropped;

    while (r.modLock.exchange(true, std::memory_order_acquire))
        std::this_thread::yield();

    for (int ci = 0; ci < mods.getNumChildren(); ++ci)
    {
        auto node = mods.getChild(ci);

        if (node.getType() == juce::Identifier("Seq"))
        {
            const juce::String id = node.getProperty("id").toString();
            for (auto& cs : r.controlSequences)
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

            if (! ModDest::isValidSourceId(a.sourceId))
            {
                dropped.add("invalid source '" + juce::String(a.sourceId) + "'");
                continue;
            }
            if (! ModDest::isValidDestinationId(a.destinationId))
            {
                dropped.add("invalid destination '" + juce::String(a.destinationId) + "'");
                continue;
            }

            if (! r.modulationMatrix.addAssignment(a))
                dropped.add("matrix rejected '" + juce::String(a.destinationId) + "' (cycle or full)");
        }
    }

    r.modLock.store(false, std::memory_order_release);
    return dropped;
}

// Clear all CS step/curve data + matrix assignments before a deserialise so
// successive preset loads don't accumulate state.
inline void clearModulators(Rhythm& r)
{
    while (r.modLock.exchange(true, std::memory_order_acquire))
        std::this_thread::yield();

    while (!r.modulationMatrix.getAssignments().empty())
        r.modulationMatrix.removeAssignment(r.modulationMatrix.getAssignments().front().id);

    for (auto& cs : r.controlSequences)
    {
        cs.stepValues.clear();
        cs.curvePoints.clear();
    }

    r.modLock.store(false, std::memory_order_release);
}

} // namespace mu_pp
