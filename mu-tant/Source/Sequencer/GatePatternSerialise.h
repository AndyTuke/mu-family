#pragma once
// Gate-pattern (de)serialise — mu-tant-specific (GatePattern is a mu-tant type,
// so this header lives under mu-tant rather than mu-core). Extracted from the
// PluginProcessor anonymous namespace so the test suite can exercise the full
// serialise→deserialise round-trip without constructing the plugin processor.
//
// tagName lets callers use different XML tags for each pattern type:
//   Gate        — amplitude gating pattern
//   FilterGate  — filter-envelope pattern
//   PitchGate   — pitch-envelope pattern

#include "Audio/SpinLock.h"   // mu-core: spin lock helpers
#include <juce_data_structures/juce_data_structures.h>
#include <algorithm>
#include <cmath>
#include <thread>
#include <limits>
#include "Sequencer/GatePattern.h"

namespace mu_tant
{

inline juce::ValueTree serialiseGate(const GatePattern& g, const char* tagName = "Gate")
{
    juce::ValueTree t(tagName);
    t.setProperty("subdiv",  (int) g.subdivision,       nullptr);
    t.setProperty("bars",    g.patternLengthBars.load(),        nullptr);
    for (const auto& e : g.envelopes)
    {
        juce::ValueTree env("Env");
        env.setProperty("start", e.startCell,         nullptr);
        env.setProperty("len",   e.lengthCells,       nullptr);
        env.setProperty("split", e.split,             nullptr);
        env.setProperty("atk",   e.attackBend,        nullptr);
        env.setProperty("dec",   e.decayBend,         nullptr);
        env.setProperty("rev",   e.reverse ? 1 : 0,  nullptr);
        t.addChild(env, -1, nullptr);
    }
    return t;
}

// Restore a gate tree into `g` (clears + rebuilds). Accepts "Gate", "FilterGate",
// and "PitchGate" tags. An invalid/absent tree clears the pattern to defaults.
// Holds editLock around the rebuild so it is safe to call without an outer suspend.
// maxCells: the demo step cap (patterns are shrunk to fit); unlimited by default.
inline void deserialiseGate(const juce::ValueTree& t, GatePattern& g,
                            int maxCells = std::numeric_limits<int>::max())
{
    const bool valid = t.isValid()
                    && (t.getType() == juce::Identifier("Gate")
                        || t.getType() == juce::Identifier("FilterGate")
                        || t.getType() == juce::Identifier("PitchGate"));

    // Capped spin matching GatingDesigner::withLock. The audio thread holds
    // editLock for at most one block (~10 ms); 1000 yields covers that comfortably.
    // If still contended after the cap (scheduler anomaly), leave the pattern
    // unchanged and return — the preset gate data stays at its previous value.
    if (! mu_core::spinLockFor(g.editLock, 1000)) return;

    g.envelopes.clear();
    if (valid)
    {
        // Only the four grid values are valid; anything else falls back to 1/16.
        const int subdiv = (int) t.getProperty("subdiv", (int) GatePattern::Subdivision::Sixteenth);
        g.subdivision = (subdiv == 4 || subdiv == 8 || subdiv == 16 || subdiv == 32)
                            ? (GatePattern::Subdivision) subdiv
                            : GatePattern::Subdivision::Sixteenth;
        g.patternLengthBars = juce::jlimit(1, GatePattern::kMaxPatternBars,
                                            (int) t.getProperty("bars", 2));
        // A non-finite float (NaN would reach std::pow in the bend curve) becomes 0, then clamps to range.
        auto cleanFloat = [](const juce::var& v, float lo, float hi)
        {
            const double d = (double) v;
            return std::isfinite(d) ? juce::jlimit(lo, hi, (float) d) : 0.0f;
        };
        const int totalCells = g.totalCells();
        for (int i = 0; i < t.getNumChildren(); ++i)
        {
            auto c = t.getChild(i);
            if (c.getType() != juce::Identifier("Env")) continue;
            GateEnvelope e;
            // Keep the region inside the grid so startCell + lengthCells cannot overflow.
            e.startCell   = juce::jlimit(0, totalCells - 1, (int) c.getProperty("start", 0));
            e.lengthCells = juce::jlimit(1, totalCells - e.startCell, (int) c.getProperty("len", 1));
            e.split       = cleanFloat(c.getProperty("split", 0.0), 0.0f, 1.0f);
            e.attackBend  = cleanFloat(c.getProperty("atk",   0.0), -1.0f, 1.0f);
            e.decayBend   = cleanFloat(c.getProperty("dec",   0.0), -1.0f, 1.0f);
            e.reverse     =        (int)    c.getProperty("rev", 0) != 0;
            // prob/loopMask/loopM/loopN properties from older presets are silently
            // ignored (the per-envelope probability / loop-N-of-M feature was removed).
            g.envelopes.push_back(e);
        }

        // Restore the sorted, non-overlapping invariant the gate lookup relies on.
        std::sort(g.envelopes.begin(), g.envelopes.end(),
                  [](const GateEnvelope& x, const GateEnvelope& y) { return x.startCell < y.startCell; });
        int nextFree = 0;
        g.envelopes.erase(std::remove_if(g.envelopes.begin(), g.envelopes.end(),
                              [&nextFree](GateEnvelope& e)
                              {
                                  if (e.startCell < nextFree) return true;
                                  nextFree = e.startCell + e.lengthCells;
                                  return false;
                              }),
                          g.envelopes.end());
    }
    else
    {
        g.subdivision       = GatePattern::Subdivision::Sixteenth;
        g.patternLengthBars = 2;
    }
    g.limitToCells(maxCells);
    g.resetGateCache();
    g.hasEnvelopes.store(!g.envelopes.empty(), std::memory_order_relaxed);
    mu_core::spinUnlock(g.editLock);
}

} // namespace mu_tant
