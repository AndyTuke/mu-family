// mu-tant modulator unit tests - covers the destination provider's
// populate/resolveId/findDropdownId callbacks, the per-voice ModulationMatrix
// + ControlSequence integration, and the audio-path "seed -> matrix.process ->
// read back" round-trip pattern that PluginProcessor::processBlock uses.

#include <juce_core/juce_core.h>
#include <unordered_map>
#include <string_view>
#include "Modulation/MuTantModDest.h"
#include "Modulation/LaneModulation.h"       // mu-core: mu_mod::resolveLane (converged routing)
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
            // uses must be findable by the provider - otherwise a saved
            // assignment from the UI would silently fail to drive the engine.
            const char* requiredKeys[] = {
                "osc1.octave", "osc1.semi", "osc1.fine", "osc1.pos",
                "osc2.octave", "osc2.semi", "osc2.fine", "osc2.pos",
                "xmod.fm", "xmod.am", "xmod.ring", "osc1.level", "osc2.level", "noise.level",
                "filter.cutoff", "filter.resonance",
                "filter2.cutoff.prop", "filter2.resonance.prop", "level"
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
            // value upward. We don't pin the exact amount - the depth scaling
            // factor in the matrix is implementation-defined - but a non-zero
            // depth assignment MUST change the seeded value.
            expect(pv["filter.cutoff"] != 1000.0f,
                   "non-zero modulation depth altered filter.cutoff from baseline");
        }

        beginTest("filter2 '.prop' destination sweeps the full proportion at full depth+source");
        {
            // Filter 2 uses the generic proportion-space convention: a destination id
            // ending in ".prop" gets depthScaleFor = 1.0, so full depth (100) x full
            // source (100) adds exactly 1.0 to the seeded 0..1 proportion.
            VoiceSlot slot;
            auto& cs = slot.controlSequences[0];
            cs.mode       = ControlSequence::Mode::Stepped;
            cs.polarity   = ControlSequence::Polarity::Unipolar;
            cs.stepValues = { 100.0f };   // full source
            cs.loopNoteValue = NoteValue::Quarter; cs.loopNoteMod = NoteMod::None; cs.loopMultiplier = 1;
            cs.stepNoteValue = NoteValue::Quarter; cs.stepNoteMod = NoteMod::None; cs.stepMultiplier = 1;

            ModulationAssignment a;
            a.id            = "cs0_to_f2cut";
            a.sourceId      = "cs0_output";
            a.destinationId = "filter2.cutoff.prop";
            a.depth         = 100.0f;
            expect(slot.modulationMatrix.addAssignment(a), "matrix accepts filter2 assignment");

            std::unordered_map<std::string_view, float> pv;
            for (int i = 0; i < kModDestCount; ++i) pv[kModDestTable[i].id] = 0.0f;
            pv["filter2.cutoff.prop"] = 0.25f;   // seeded base proportion

            slot.modulationMatrix.process(slot.controlSequences, 0.0, pv);
            expectWithinAbsoluteError(pv["filter2.cutoff.prop"], 1.25f, 1.0e-4f,
                   "full depth+source adds 1.0 to the proportion (scale 1.0)");
        }

        beginTest("registered product depth-scale is proportion-space (osc1.semi -> scale 1.0)");
        {
            // mu-tant's engine dests are now routed through mu_mod::resolveLane, which seeds
            // proportions (0..1) - so every registered scale is 1.0 (not the old display-unit
            // 24). Verify the registration overrides mu-core's 0..100 default of 100: a full
            // depth+source mod adds exactly 1.0 to a proportion seed.
            registerDepthScales();   // idempotent (call_once); normally the processor ctor calls it

            VoiceSlot slot;
            auto& cs = slot.controlSequences[0];
            cs.mode       = ControlSequence::Mode::Stepped;
            cs.polarity   = ControlSequence::Polarity::Unipolar;
            cs.stepValues = { 100.0f };
            cs.loopNoteValue = NoteValue::Quarter; cs.loopNoteMod = NoteMod::None; cs.loopMultiplier = 1;
            cs.stepNoteValue = NoteValue::Quarter; cs.stepNoteMod = NoteMod::None; cs.stepMultiplier = 1;

            ModulationAssignment a;
            a.id = "cs0_to_semi"; a.sourceId = "cs0_output"; a.destinationId = "osc1.semi"; a.depth = 100.0f;
            expect(slot.modulationMatrix.addAssignment(a), "matrix accepts osc1.semi assignment");

            std::unordered_map<std::string_view, float> pv;
            for (int i = 0; i < kModDestCount; ++i) pv[kModDestTable[i].id] = 0.0f;
            pv["osc1.semi"] = 0.5f;   // mid-range proportion
            slot.modulationMatrix.process(slot.controlSequences, 0.0, pv);
            expectWithinAbsoluteError(pv["osc1.semi"], 1.5f, 1.0e-4f,
                   "full depth+source adds the proportion-space scale (1.0), not the 100 default");
        }

        beginTest("resolveLane converges osc1.semi to its range rail (proportion-clamped)");
        {
            // End-to-end check of the converged path: a full-depth unipolar mod on osc1.semi
            // (range -12..12) seeds the param's mid proportion and sweeps to the +12 rail,
            // clamped - the behaviour mu-tant's renderVoice now relies on via resolveLane.
            registerDepthScales();

            VoiceSlot slot;
            auto& cs = slot.controlSequences[0];
            cs.mode       = ControlSequence::Mode::Stepped;
            cs.polarity   = ControlSequence::Polarity::Unipolar;
            cs.stepValues = { 100.0f };
            cs.loopNoteValue = NoteValue::Quarter; cs.loopNoteMod = NoteMod::None; cs.loopMultiplier = 1;
            cs.stepNoteValue = NoteValue::Quarter; cs.stepNoteMod = NoteMod::None; cs.stepMultiplier = 1;
            ModulationAssignment a;
            a.id = "cs0_to_semi"; a.sourceId = "cs0_output"; a.destinationId = "osc1.semi"; a.depth = 100.0f;
            expect(slot.modulationMatrix.addAssignment(a), "matrix accepts osc1.semi assignment");

            const juce::NormalisableRange<float> semiRange(-12.0f, 12.0f, 1.0f);
            std::atomic<float> semiAtom { 0.0f };   // base value 0 -> proportion 0.5
            const char* ids[1] { "osc1.semi" };
            const std::atomic<float>* atoms[1] { &semiAtom };
            std::unordered_map<std::string_view, float> pv;
            pv.emplace(std::string_view("osc1.semi"), 0.0f);
            float out[1] {};
            mu_mod::resolveLane(&slot, 0.0, 1, ids, atoms, &semiRange, pv, out);
            expectWithinAbsoluteError(out[0], 12.0f, 1.0e-3f,
                   "full-depth mod from base 0 sweeps osc1.semi to its +12 rail");
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
            // Meta-modulation source format is "assign_{id}_depth" - assignment B
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

            // Now mutate a so it sources b's depth - should be rejected.
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
