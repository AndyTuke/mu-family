// Modulator (de)serialise round-trip test.
//
// Constructs a Rhythm with:
//   - A non-default ControlSequence (Stepped mode, Bipolar, custom timing,
//     populated stepValues + curvePoints)
//   - A ModulationAssignment with non-default depth + curve
// Passes through serialiseModulators -> deserialiseModulators into a fresh
// Rhythm, then asserts every member matches. Confirms the enum-name path
// (kModulatorModeNames, kModulatorPolarityNames, kNoteValueNames, kNoteModNames)
// is symmetric.

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include "Sequencer/Rhythm.h"
#include "../Persistence/ModulatorSerialise.h"

using mu_pp::serialiseModulators;
using mu_pp::deserialiseModulators;
using mu_pp::clearModulators;

class ModulatorSerialiseTest : public juce::UnitTest
{
public:
    ModulatorSerialiseTest() : juce::UnitTest ("serialiseModulators / deserialiseModulators round-trip", "Preset") {}

    void runTest() override
    {
        beginTest ("ControlSequence fields round-trip");

        Rhythm src;

        // Configure cs0 with non-default values
        auto& cs = src.controlSequences[0];
        cs.mode           = ControlSequence::Mode::Stepped;
        cs.polarity       = ControlSequence::Polarity::Bipolar;
        cs.loopNoteValue  = NoteValue::Eighth;
        cs.loopNoteMod    = NoteMod::Triplet;
        cs.loopMultiplier = 3;
        cs.stepNoteValue  = NoteValue::Sixteenth;
        cs.stepNoteMod    = NoteMod::Dotted;
        cs.stepMultiplier = 2;
        cs.stepValues     = { 50.0f, -25.0f, 75.0f, 0.0f };

        ControlSequence::CurvePoint pt;
        pt.x = 0.25f;  pt.y = 0.5f;  pt.hasBezierHandle = true;
        pt.handleX = 0.1f;  pt.handleY = -0.2f;
        cs.curvePoints = { pt };

        // Serialise and deserialise into a fresh Rhythm
        juce::ValueTree mods = serialiseModulators (src);
        Rhythm dst;
        clearModulators (dst);
        auto dropped = deserialiseModulators (mods, dst);

        expect (dropped.isEmpty(), "No assignments should be dropped");

        const auto& cs2 = dst.controlSequences[0];
        expectEquals ((int)cs2.mode,           (int)ControlSequence::Mode::Stepped,   "mode");
        expectEquals ((int)cs2.polarity,        (int)ControlSequence::Polarity::Bipolar, "polarity");
        expectEquals ((int)cs2.loopNoteValue,   (int)NoteValue::Eighth,                "loopNoteValue");
        expectEquals ((int)cs2.loopNoteMod,     (int)NoteMod::Triplet,                 "loopNoteMod");
        expectEquals (cs2.loopMultiplier,       3,                                     "loopMultiplier");
        expectEquals ((int)cs2.stepNoteValue,   (int)NoteValue::Sixteenth,             "stepNoteValue");
        expectEquals ((int)cs2.stepNoteMod,     (int)NoteMod::Dotted,                  "stepNoteMod");
        expectEquals (cs2.stepMultiplier,       2,                                     "stepMultiplier");

        expect (cs2.stepValues.size() == 4, "stepValues count");
        if (cs2.stepValues.size() == 4)
        {
            expectWithinAbsoluteError (cs2.stepValues[0],  50.0f, 1e-4f, "stepValues[0]");
            expectWithinAbsoluteError (cs2.stepValues[1], -25.0f, 1e-4f, "stepValues[1]");
            expectWithinAbsoluteError (cs2.stepValues[2],  75.0f, 1e-4f, "stepValues[2]");
            expectWithinAbsoluteError (cs2.stepValues[3],   0.0f, 1e-4f, "stepValues[3]");
        }

        expect (cs2.curvePoints.size() == 1, "curvePoints count");
        if (cs2.curvePoints.size() == 1)
        {
            const auto& p2 = cs2.curvePoints[0];
            expectWithinAbsoluteError (p2.x,       0.25f,  1e-4f, "curvePoint.x");
            expectWithinAbsoluteError (p2.y,       0.5f,   1e-4f, "curvePoint.y");
            expect  (p2.hasBezierHandle,                           "curvePoint.hasBezierHandle");
            expectWithinAbsoluteError (p2.handleX,  0.1f,  1e-4f, "curvePoint.handleX");
            expectWithinAbsoluteError (p2.handleY, -0.2f,  1e-4f, "curvePoint.handleY");
        }

        beginTest ("ModulationAssignment round-trip");

        Rhythm src2;
        ModulationAssignment a;
        a.id            = "test-asgn-1";
        a.sourceId      = "cs0_output";
        a.destinationId = "filter.cutoff";
        a.depth         = 42.5f;
        a.curve         = -30.0f;
        src2.modulationMatrix.addAssignment (a);

        juce::ValueTree mods2 = serialiseModulators (src2);
        Rhythm dst2;
        clearModulators (dst2);
        auto dropped2 = deserialiseModulators (mods2, dst2);

        expect (dropped2.isEmpty(), "No assignments should be dropped");
        const auto& assignments = dst2.modulationMatrix.getAssignments();
        expect (assignments.size() == 1, "Should have exactly one assignment");
        if (assignments.size() == 1)
        {
            const auto& a2 = assignments[0];
            expect (juce::String(a2.sourceId)      == "cs0_output",    "sourceId");
            expect (juce::String(a2.destinationId) == "filter.cutoff", "destinationId");
            expectWithinAbsoluteError (a2.depth,  42.5f, 1e-4f, "depth");
            expectWithinAbsoluteError (a2.curve, -30.0f, 1e-4f, "curve");
        }

        beginTest ("Invalid source/dest IDs are rejected with diagnostics");

        juce::ValueTree badMods ("Modulators");
        juce::ValueTree badAsgn ("Asgn");
        badAsgn.setProperty ("id",    "bad-1",       nullptr);
        badAsgn.setProperty ("src",   "not_a_source", nullptr);
        badAsgn.setProperty ("dest",  "filter.cutoff", nullptr);
        badAsgn.setProperty ("depth", 50.0f,           nullptr);
        badAsgn.setProperty ("curve", 0.0f,             nullptr);
        badMods.addChild (badAsgn, -1, nullptr);

        Rhythm dst3;
        auto dropped3 = deserialiseModulators (badMods, dst3);
        expect (!dropped3.isEmpty(), "Invalid source should produce a drop message");

        beginTest ("Enum names written as strings in the ValueTree");

        // Confirm that mode / polarity / loopNV are stored as name strings, not ints.
        Rhythm src3;
        src3.controlSequences[0].mode     = ControlSequence::Mode::Smooth;
        src3.controlSequences[0].polarity = ControlSequence::Polarity::Unipolar;
        juce::ValueTree mods3 = serialiseModulators (src3);
        auto seqNode = mods3.getChildWithProperty ("id", "cs0");
        expect (seqNode.isValid(), "cs0 Seq node should exist");
        if (seqNode.isValid())
        {
            expect (seqNode.getProperty ("mode").toString()     == "Smooth",   "mode should be name string");
            expect (seqNode.getProperty ("polarity").toString() == "Unipolar", "polarity should be name string");
        }

        beginTest ("Finding-1 belt: a Smooth Seq with only <Step>s self-heals to Stepped on load");
        {
            // External / AI-authored mismatch: mode says Smooth but the data is Steps.
            // The loader must flip the mode to match the data actually present so the
            // sequence isn't silently inert.
            juce::ValueTree modsTree ("Modulators");
            juce::ValueTree seq ("Seq");
            seq.setProperty ("id", "cs0", nullptr);
            seq.setProperty ("mode", "Smooth", nullptr);   // mismatch - no <Point>s below
            for (float v : { 0.0f, 80.0f, -40.0f, 20.0f })
            {
                juce::ValueTree st ("Step");
                st.setProperty ("v", v, nullptr);
                seq.addChild (st, -1, nullptr);
            }
            modsTree.addChild (seq, -1, nullptr);

            Rhythm dstR;
            clearModulators (dstR);
            deserialiseModulators (modsTree, dstR);

            const auto& loadedCs = dstR.controlSequences[0];
            expectEquals ((int) loadedCs.mode, (int) ControlSequence::Mode::Stepped,
                "mode must self-heal Smooth -> Stepped when only <Step>s are present");
            expect (loadedCs.stepValues.size() == 4, "step values still loaded");
            expect (loadedCs.curvePoints.empty(), "no curve points were present");
        }

        beginTest ("Finding-1 braces: evaluate() falls back to the populated array when the active mode is empty");
        {
            // Same data, two modes; the Smooth one has no curvePoints, so evaluate()
            // must fall back to the step data rather than output a constant 0.
            ControlSequence stepped, smoothNoCurve;
            for (auto* csPtr : { &stepped, &smoothNoCurve })
            {
                csPtr->loopNoteValue  = NoteValue::Quarter;  csPtr->loopMultiplier = 1;
                csPtr->stepNoteValue  = NoteValue::Sixteenth; csPtr->stepMultiplier = 1;
                csPtr->polarity       = ControlSequence::Polarity::Bipolar;
                csPtr->stepValues     = { 100.0f, -100.0f, 50.0f, -50.0f };
            }
            stepped.mode       = ControlSequence::Mode::Stepped;
            smoothNoCurve.mode = ControlSequence::Mode::Smooth;   // no curvePoints

            bool anyNonZero = false;
            for (double beat = 0.0; beat < 1.0; beat += 0.1)
            {
                const float steppedVal = stepped.evaluate (beat);
                const float b = smoothNoCurve.evaluate (beat);
                expectWithinAbsoluteError (b, steppedVal, 1.0e-4f,
                    "Smooth-with-no-curve must evaluate identically to Stepped at beat " + juce::String (beat));
                if (std::abs (b) > 1.0e-3f) anyNonZero = true;
            }
            expect (anyNonZero, "fallback must produce non-zero output, not a silent constant 0");
        }
    }
};

static ModulatorSerialiseTest modulatorSerialiseTest;
