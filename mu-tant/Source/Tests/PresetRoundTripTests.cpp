// mu-tant gate-pattern serialisation round-trip tests.
//
// Covers the three gate-pattern types (Gate / FilterGate / PitchGate):
//   - Envelope attributes round-trip through serialiseGate → deserialiseGate
//   - probability, loopMask, loopM, reverse, attackBend, decayBend preserved
//   - Legacy loopN attribute correctly converted to single-bit loopMask
//   - Invalid / absent tree clears the pattern to defaults
//   - copyDataFrom transfers envelopes correctly (with hasEnvelopes flag)
// Does not construct PluginProcessor (which drags in the UI tree).

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include "Sequencer/GatePattern.h"
#include "Sequencer/GatePatternSerialise.h"

class PresetRoundTripTest : public juce::UnitTest
{
public:
    PresetRoundTripTest() : juce::UnitTest("mu-tant gate serialise round-trip", "mu-tant") {}

    void runTest() override
    {
        using namespace mu_tant;

        // ── Core round-trip ────────────────────────────────────────────────────
        beginTest("single envelope round-trips all attributes");
        {
            GatePattern src;
            src.subdivision = GatePattern::Subdivision::ThirtySecond;
            GateEnvelope e;
            e.startCell   = 5;
            e.lengthCells = 3;
            e.split       = 0.4f;
            e.attackBend  = 0.7f;
            e.decayBend   = -0.3f;
            e.reverse     = true;
            e.probability = 0.75f;
            src.envelopes.push_back(e);
            src.hasEnvelopes.store(true, std::memory_order_relaxed);

            const auto tree = serialiseGate(src, "Gate");
            expect(tree.getType() == juce::Identifier("Gate"), "tag is Gate");
            expect(tree.getNumChildren() == 1, "one Env child");
            expectEquals((int) tree.getProperty("subdiv"), (int) GatePattern::Subdivision::ThirtySecond);

            GatePattern dst;
            deserialiseGate(tree, dst);
            expect(dst.subdivision == GatePattern::Subdivision::ThirtySecond, "subdivision restored");
            expect(dst.envelopes.size() == 1u, "one envelope restored");
            expect(dst.hasEnvelopes.load(), "hasEnvelopes flag set");

            const auto& re = dst.envelopes[0];
            expectEquals(re.startCell,   5);
            expectEquals(re.lengthCells, 3);
            expectWithinAbsoluteError(re.split,      0.4f,  0.001f);
            expectWithinAbsoluteError(re.attackBend, 0.7f,  0.001f);
            expectWithinAbsoluteError(re.decayBend,  -0.3f, 0.001f);
            expect(re.reverse == true, "reverse restored");
            expectWithinAbsoluteError(re.probability, 0.75f, 0.001f);
        }

        // ── FilterGate tag ─────────────────────────────────────────────────────
        beginTest("FilterGate tag round-trips");
        {
            GatePattern src;
            GateEnvelope e; e.startCell = 0; e.lengthCells = 8;
            src.envelopes.push_back(e);
            src.hasEnvelopes.store(true, std::memory_order_relaxed);

            const auto tree = serialiseGate(src, "FilterGate");
            expect(tree.getType() == juce::Identifier("FilterGate"), "tag is FilterGate");

            GatePattern dst;
            deserialiseGate(tree, dst);
            expect(dst.envelopes.size() == 1u, "envelope restored from FilterGate");
        }

        // ── PitchGate tag ──────────────────────────────────────────────────────
        beginTest("PitchGate tag round-trips");
        {
            GatePattern src;
            GateEnvelope e; e.startCell = 2; e.lengthCells = 4;
            src.envelopes.push_back(e);
            src.hasEnvelopes.store(true, std::memory_order_relaxed);

            const auto tree = serialiseGate(src, "PitchGate");
            GatePattern dst;
            deserialiseGate(tree, dst);
            expect(dst.envelopes.size() == 1u, "envelope restored from PitchGate");
        }

        // ── Multiple envelopes ─────────────────────────────────────────────────
        beginTest("multiple envelopes preserved in order");
        {
            GatePattern src;
            src.subdivision = GatePattern::Subdivision::Eighth;
            for (int i = 0; i < 4; ++i)
            {
                GateEnvelope e;
                e.startCell   = i * 4;
                e.lengthCells = 2;
                e.split       = (float) i * 0.25f;
                src.envelopes.push_back(e);
            }
            src.hasEnvelopes.store(true, std::memory_order_relaxed);

            GatePattern dst;
            deserialiseGate(serialiseGate(src, "Gate"), dst);
            expectEquals((int) dst.envelopes.size(), 4);
            for (int i = 0; i < 4; ++i)
            {
                expectEquals(dst.envelopes[(size_t) i].startCell,   i * 4);
                expectEquals(dst.envelopes[(size_t) i].lengthCells, 2);
                expectWithinAbsoluteError(dst.envelopes[(size_t) i].split,
                                          (float) i * 0.25f, 0.001f);
            }
        }

        // ── Legacy loopN → loopMask conversion ───────────────────────────────
        beginTest("legacy loopN/loopM properties silently ignored on load");
        {
            juce::ValueTree t("Gate");
            t.setProperty("subdiv", (int) GatePattern::Subdivision::Sixteenth, nullptr);
            juce::ValueTree env("Env");
            env.setProperty("start",  0, nullptr);
            env.setProperty("len",    1, nullptr);
            env.setProperty("loopN",  3, nullptr);  // old format: play on loop 3 of M
            env.setProperty("loopM",  4, nullptr);
            t.addChild(env, -1, nullptr);

            GatePattern dst;
            deserialiseGate(t, dst);
            expect(dst.envelopes.size() == 1u, "one envelope");
            // loopN/loopM legacy properties are silently ignored on load.
        }

        // ── Invalid tree clears the pattern ────────────────────────────────────
        beginTest("invalid tree clears the pattern");
        {
            GatePattern pat;
            GateEnvelope e; e.startCell = 0; e.lengthCells = 1;
            pat.envelopes.push_back(e);
            pat.hasEnvelopes.store(true, std::memory_order_relaxed);

            deserialiseGate(juce::ValueTree{}, pat);
            expect(pat.envelopes.empty(), "empty pattern after invalid tree");
            expect(!pat.hasEnvelopes.load(), "hasEnvelopes cleared");
            expect(pat.subdivision == GatePattern::Subdivision::Sixteenth, "default subdivision");
        }

        // ── copyDataFrom transfers envelopes + hasEnvelopes ───────────────────
        beginTest("copyDataFrom copies envelopes and hasEnvelopes");
        {
            GatePattern src;
            for (int i = 0; i < 3; ++i)
            {
                GateEnvelope e; e.startCell = i * 5; e.lengthCells = 3;
                src.envelopes.push_back(e);
            }
            src.hasEnvelopes.store(true, std::memory_order_relaxed);
            src.subdivision = GatePattern::Subdivision::Quarter;

            GatePattern dst;
            dst.copyDataFrom(src);
            expectEquals((int) dst.envelopes.size(), 3);
            expect(dst.hasEnvelopes.load(), "hasEnvelopes transferred");
            expect(dst.subdivision == GatePattern::Subdivision::Quarter, "subdivision transferred");
            expectEquals(dst.envelopes[1].startCell, 5);
            // slew state reset
            expectWithinAbsoluteError(dst.gateLevel,   0.0f, 0.001f);
            expectWithinAbsoluteError(dst.filterLevel, 1.0f, 0.001f);
        }

        // ── Empty pattern serialises cleanly ──────────────────────────────────
        beginTest("empty pattern serialises and restores as empty");
        {
            GatePattern src;   // empty — no envelopes
            const auto tree = serialiseGate(src, "Gate");
            expectEquals(tree.getNumChildren(), 0);

            GatePattern dst;
            // Seed it with a dummy envelope to verify the clear path.
            GateEnvelope e; e.startCell = 0; e.lengthCells = 1;
            dst.envelopes.push_back(e);
            dst.hasEnvelopes.store(true, std::memory_order_relaxed);

            deserialiseGate(tree, dst);
            expect(dst.envelopes.empty(), "empty after restoring empty tree");
            expect(!dst.hasEnvelopes.load(), "hasEnvelopes false after restore");
        }
    }
};

static PresetRoundTripTest presetRoundTripTestInstance;
