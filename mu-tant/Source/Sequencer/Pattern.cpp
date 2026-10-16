#include "Sequencer/Pattern.h"
#include "UI/PatternHost.h"                  // the layer's host: the wavetable bank a saved path resolves into
#include "Modulation/MuTantModDest.h"        // the modulation target table
#include "Modulation/ModulatorSerialise.h"   // mu-core: shared modulator (de)serialise
#include "Sequencer/GatePatternSerialise.h"  // the gate (de)serialise

#include <limits>

// The Pattern layer's persistence (its node's non-parameter data) and its stepped-pitch flags.
namespace mu_tant
{

namespace
{
    // Drops assignments to destinations this layer type does not expose (source ids are ControlSequence
    // ids, left unchecked).
    bool isValidModDest(const std::string& id)
    {
        for (int i = 0; i < kModDestCount; ++i)
            if (id == kModDestTable[i].id) return true;
        return false;
    }

    // True if a Stepped ControlSequence of `layer` is assigned to `destId`: those snap pitch to semitones
    // (melodies); smooth sources and the envelope glide. Compares "csN_output" against each sequence id
    // without building strings.
    bool hasSteppedSource(const Layer& layer, const char* destId)
    {
        for (const auto& a : layer.modulationMatrix.getAssignments())
        {
            if (a.destinationId != destId) continue;
            for (const auto& cs : layer.controlSequences)
            {
                if (cs.mode != ControlSequence::Mode::Stepped) continue;
                const auto& sid = cs.id;
                if (a.sourceId.size() == sid.size() + 7
                    && a.sourceId.compare(0, sid.size(), sid) == 0
                    && a.sourceId.compare(sid.size(), 7, "_output") == 0)
                    return true;
            }
        }
        return false;
    }
}

// The voice's non-parameter data onto its node: the user wavetable paths, the modulators and the three
// envelope patterns, in this order (saved files compare byte for byte).
void Pattern::writeExtras(juce::ValueTree& node) const
{
    if (osc1UserPath.isNotEmpty()) node.setProperty("o1WtPath", osc1UserPath, nullptr);
    if (osc2UserPath.isNotEmpty()) node.setProperty("o2WtPath", osc2UserPath, nullptr);
    node.appendChild(mu_pp::serialiseModulators(*this),          nullptr);
    node.appendChild(serialiseGate(gate),                         nullptr);
    node.appendChild(serialiseGate(filterGate, "FilterGate"),     nullptr);
    node.appendChild(serialiseGate(pitchGate,  "PitchGate"),      nullptr);
}

// The voice's non-parameter data from its node. Absent children clear (an invalid tree empties a gate / the
// modulators); per-pattern spin locks and modLock guard the audio thread, so no layersLock is needed.
void Pattern::applyExtras(const juce::ValueTree& node)
{
    mu_pp::clearModulators(*this);
    mu_pp::deserialiseModulators(node.getChildWithName("Modulators"), *this, {}, isValidModDest);
    refreshPitchQuantFlags();   // the assignments changed

    const int maxCells = std::numeric_limits<int>::max();
    deserialiseGate(node.getChildWithName("Gate"),       gate,       maxCells);
    deserialiseGate(node.getChildWithName("FilterGate"), filterGate, maxCells);
    deserialiseGate(node.getChildWithName("PitchGate"),  pitchGate,  maxCells);

    // A missing file keeps its path (the UI shows "missing") but resolves to -1, so the oscillator falls back
    // to its factory selection.
    auto resolve = [this](const juce::String& path, juce::String& storedPath, std::atomic<int>& index)
    {
        storedPath = path;
        index.store(path.isNotEmpty() && host.ptr != nullptr ? host.ptr->resolvePatternWavetable(path) : -1);
    };
    resolve(node.getProperty("o1WtPath").toString(), osc1UserPath, osc1UserIndex);
    resolve(node.getProperty("o2WtPath").toString(), osc2UserPath, osc2UserIndex);
}

void Pattern::refreshPitchQuantFlags()
{
    osc1SemiStepped.store(hasSteppedSource(*this, "osc1.semi"), std::memory_order_relaxed);
    osc2SemiStepped.store(hasSteppedSource(*this, "osc2.semi"), std::memory_order_relaxed);
}

} // namespace mu_tant
