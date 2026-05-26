// Modulator (de)serialise round-trip test (#455).
//
// Constructs a Rhythm with:
//   - A non-default ControlSequence (Stepped mode, Bipolar, custom timing,
//     populated stepValues + curvePoints)
//   - A ModulationAssignment with non-default depth + curve
// Passes through serialiseModulators → deserialiseModulators into a fresh
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
    }
};

static ModulatorSerialiseTest modulatorSerialiseTest;
