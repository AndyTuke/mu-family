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

#include <juce_data_structures/juce_data_structures.h>
#include <thread>
#include "Sequencer/GatePattern.h"

namespace mu_tant
{

inline juce::ValueTree serialiseGate(const GatePattern& g, const char* tagName = "Gate")
{
    juce::ValueTree t(tagName);
    t.setProperty("subdiv", (int) g.subdivision, nullptr);
    for (const auto& e : g.envelopes)
    {
        juce::ValueTree env("Env");
        env.setProperty("start",    e.startCell,         nullptr);
        env.setProperty("len",      e.lengthCells,       nullptr);
        env.setProperty("split",    e.split,             nullptr);
        env.setProperty("atk",      e.attackBend,        nullptr);
        env.setProperty("dec",      e.decayBend,         nullptr);
        env.setProperty("rev",      e.reverse ? 1 : 0,  nullptr);
        env.setProperty("prob",     e.probability,       nullptr);
        env.setProperty("loopMask", (int) e.loopMask,   nullptr);
        env.setProperty("loopM",    e.loopM,             nullptr);
        t.addChild(env, -1, nullptr);
    }
    return t;
}

// Restore a gate tree into `g` (clears + rebuilds). Accepts "Gate", "FilterGate",
// and "PitchGate" tags. An invalid/absent tree clears the pattern to defaults.
// Holds editLock around the rebuild so it is safe to call without an outer suspend.
inline void deserialiseGate(const juce::ValueTree& t, GatePattern& g)
{
    const bool valid = t.isValid()
                    && (t.getType() == juce::Identifier("Gate")
                        || t.getType() == juce::Identifier("FilterGate")
                        || t.getType() == juce::Identifier("PitchGate"));

    // Capped spin matching GatingDesigner::withLock. The audio thread holds
    // editLock for at most one block (~10 ms); 1000 yields covers that comfortably.
    // If still contended after the cap (scheduler anomaly), leave the pattern
    // unchanged and return — the preset gate data stays at its previous value.
    {
        constexpr int kMaxSpins = 1000;
        bool acquired = false;
        for (int i = 0; i < kMaxSpins; ++i)
        {
            bool expected = false;
            if (g.editLock.compare_exchange_strong(expected, true, std::memory_order_acquire))
            { acquired = true; break; }
            std::this_thread::yield();
        }
        if (!acquired) return;
    }

    g.envelopes.clear();
    if (valid)
    {
        g.subdivision = (GatePattern::Subdivision)(int)
            t.getProperty("subdiv", (int) GatePattern::Subdivision::Sixteenth);
        for (int i = 0; i < t.getNumChildren(); ++i)
        {
            auto c = t.getChild(i);
            if (c.getType() != juce::Identifier("Env")) continue;
            GateEnvelope e;
            e.startCell   =        (int)    c.getProperty("start", 0);
            e.lengthCells = juce::jmax(1, (int) c.getProperty("len", 1));
            e.split       = (float)(double) c.getProperty("split", 0.0);
            e.attackBend  = (float)(double) c.getProperty("atk",   0.0);
            e.decayBend   = (float)(double) c.getProperty("dec",   0.0);
            e.reverse     =        (int)    c.getProperty("rev", 0) != 0;
            e.probability = juce::jlimit(0.0f, 1.0f,
                                (float)(double) c.getProperty("prob", 1.0));
            // loopMask: new format, or convert legacy loopN to a single-bit mask.
            if (c.hasProperty("loopMask"))
                e.loopMask = (uint8_t) juce::jlimit(1, 255, (int) c.getProperty("loopMask", 1));
            else
            {
                const int legacyN = juce::jlimit(1, 8, (int) c.getProperty("loopN", 1));
                e.loopMask = (uint8_t)(1 << (legacyN - 1));
            }
            e.loopM = juce::jlimit(1, 8, (int) c.getProperty("loopM", 1));
            g.envelopes.push_back(e);
        }
    }
    else
    {
        g.subdivision = GatePattern::Subdivision::Sixteenth;
    }
    g.resetGateCache();
    g.hasEnvelopes.store(!g.envelopes.empty(), std::memory_order_relaxed);
    g.editLock.store(false, std::memory_order_release);
}

} // namespace mu_tant
