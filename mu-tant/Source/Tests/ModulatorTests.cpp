// mu-tant modulator unit tests — covers the destination provider's
// populate/resolveId/findDropdownId callbacks, the per-voice ModulationMatrix
// + ControlSequence integration, and the audio-path "seed → matrix.process →
// read back" round-trip pattern that PluginProcessor::processBlock uses.

#include <juce_core/juce_core.h>
#include <unordered_map>
#include <string_view>
#include "Modulation/MuTantModDest.h"
#include "Sequencer/VoiceSlot.h"
#include "Sequencer/ControlSequence.h"
#include "Modulation/ModulationMatrix.h"
#include "Modulation/ModulationAssignment.h"

class ModulatorTest : public juce::UnitTest
{
public:
    ModulatorTest() : juce::UnitTest("mu-tant modulator", "mu-tant") {}

    void runTest() override
    {
        using namespace mu_tant;

        // ── Destination provider round-trips ────────────────────────────────
        beginTest("destination provider: resolveId + findDropdownId form a round-trip");
        {
            auto provider = makeModDestProvider();
            for (int i = 0; i < kModDestCount; ++i)
            {
                const int dropdownId = i + 1;
                const std::string destStr = provider.resolveId(dropdownId);
                expect(!destStr.empty(),
                       "entry " + juce::String(i) + " resolves to a non-empty string");
                expect(provider.findDropdownId(destStr) == dropdownId,
                       "round-trip ID " + juce::String(dropdownId)
                            + " (" + juce::String(destStr) + ")");
            }
        }

        beginTest("destination provider: invalid IDs return empty / 0");
        {
            auto provider = makeModDestProvider();
            expect(provider.resolveId(0).empty(),         "dropdown id 0 is empty");
            expect(provider.resolveId(99999).empty(),     "dropdown id past table end is empty");
            expect(provider.findDropdownId("nope") == 0,  "unknown dest string returns 0");
            expect(provider.findDropdownId("") == 0,      "empty dest string returns 0");
        }

        beginTest("destination table contains the audio-thread keys mu-tant needs");
        {
            // Every key the PluginProcessor::processBlock seed-and-read pattern
            // uses must be findable by the provider — otherwise a saved
            // assignment from the UI would silently fail to drive the engine.
            const char* requiredKeys[] = {
                "osc1.octave", "osc1.semi", "osc1.fine", "osc1.pos",
                "osc2.octave", "osc2.semi", "osc2.fine", "osc2.pos",
                "xmod", "osc1.level", "osc2.level", "noise.level",
                "filter.cutoff", "filter.resonance", "level"
            };
            auto provider = makeModDestProvider();
            for (auto* k : requiredKeys)
                expect(provider.findDropdownId(k) > 0,
                       juce::String("required key '") + k + "' is in the destination table");
        }

        // ── Per-voice ControlSequence + ModulationMatrix integration ────────
        beginTest("VoiceSlot default-constructs with 8 named ControlSequences");
        {
            VoiceSlot slot;
            expect((int) slot.controlSequences.size() == VoiceSlot::MaxControlSequences,
                   "slot has MaxControlSequences entries");
            for (int i = 0; i < (int) slot.controlSequences.size(); ++i)
            {
                const std::string expected = "cs" + std::to_string(i);
                expect(slot.controlSequences[(size_t) i].id == expected,
                       "CS " + juce::String(i) + " ID is 'cs" + juce::String(i) + "'");
            }
        }

        beginTest("modulation matrix routes a stepped CS to filter.cutoff");
        {
            VoiceSlot slot;
            // Configure cs0 as a single-step Stepped CS with value 50 (unipolar).
            // At phase 0 the stepped evaluator outputs the first step value.
            auto& cs = slot.controlSequences[0];
            cs.mode       = ControlSequence::Mode::Stepped;
            cs.polarity   = ControlSequence::Polarity::Unipolar;
            cs.stepValues = { 50.0f };
            cs.loopNoteValue = NoteValue::Quarter; cs.loopNoteMod = NoteMod::None; cs.loopMultiplier = 1;
            cs.stepNoteValue = NoteValue::Quarter; cs.stepNoteMod = NoteMod::None; cs.stepMultiplier = 1;

            ModulationAssignment a;
            a.id            = "cs0_to_cutoff";
            a.sourceId      = "cs0_output";
            a.destinationId = "filter.cutoff";
            a.depth         = 100.0f;   // full depth
            expect(slot.modulationMatrix.addAssignment(a), "matrix accepts assignment");

            // Seed paramValues with a baseline cutoff.
            std::unordered_map<std::string_view, float> pv;
            for (int i = 0; i < kModDestCount; ++i) pv[kModDestTable[i].id] = 0.0f;
            pv["filter.cutoff"] = 1000.0f;

            slot.modulationMatrix.process(slot.controlSequences, 0.0, pv);

            // Stepped CS at phase 0 with value 50 + full depth (100) drives the
            // value upward. We don't pin the exact amount — the depth scaling
            // factor in the matrix is implementation-defined — but a non-zero
            // depth assignment MUST change the seeded value.
            expect(pv["filter.cutoff"] != 1000.0f,
                   "non-zero modulation depth altered filter.cutoff from baseline");
        }

        beginTest("modulation matrix is a no-op when no assignments exist");
        {
            VoiceSlot slot;
            std::unordered_map<std::string_view, float> pv;
            pv["level"] = -6.0f;
            pv["filter.cutoff"] = 800.0f;
            slot.modulationMatrix.process(slot.controlSequences, 0.0, pv);
            expect(pv["level"] == -6.0f,        "level unchanged with empty matrix");
            expect(pv["filter.cutoff"] == 800.0f,"cutoff unchanged with empty matrix");
        }

        beginTest("matrix rejects circular dependencies");
        {
            // Meta-modulation source format is "assign_{id}_depth" — assignment B
            // sources assignment A's depth, and assignment A sources B's depth.
            // The second add must be rejected.
            VoiceSlot slot;
            ModulationAssignment a;
            a.id = "a";
            a.sourceId = "cs0_output";
            a.destinationId = "filter.cutoff";
            a.depth = 50.0f;
            expect(slot.modulationMatrix.addAssignment(a), "first assignment accepted");

            ModulationAssignment b;
            b.id = "b";
            b.sourceId = "assign_a_depth";
            b.destinationId = "level";
            b.depth = 10.0f;
            expect(slot.modulationMatrix.addAssignment(b), "meta-mod assignment accepted");

            // Now mutate a so it sources b's depth — should be rejected.
            slot.modulationMatrix.removeAssignment("a");
            ModulationAssignment cyclic;
            cyclic.id = "a";
            cyclic.sourceId = "assign_b_depth";
            cyclic.destinationId = "filter.cutoff";
            cyclic.depth = 50.0f;
            expect(!slot.modulationMatrix.addAssignment(cyclic),
                   "circular dependency rejected");
        }
    }
};

static ModulatorTest modulatorTestInstance;
