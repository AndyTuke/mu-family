// mu-tant modulator-persistence tests — exercises the shared mu-core modulator
// (de)serialise (mu_pp) the way mu-tant uses it: a VoiceSlot's
// ControlSequences + ModulationMatrix assignments round-trip through a ValueTree
// with mu-tant's destination validator. Guards against the silent loss the
// fix addressed.

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <string>
#include "Sequencer/VoiceSlot.h"
#include "Sequencer/ControlSequence.h"
#include "Modulation/ModulationMatrix.h"
#include "Modulation/ModulationAssignment.h"
#include "Modulation/ModulatorSerialise.h"   // mu-core mu_pp::serialise/deserialise
#include "Modulation/MuTantModDest.h"

class MuTantPersistTest : public juce::UnitTest
{
public:
    MuTantPersistTest() : juce::UnitTest("mu-tant modulator persist", "mu-tant") {}

    void runTest() override
    {
        using namespace mu_tant;

        auto destValid = [](const std::string& id)
        {
            for (int i = 0; i < kModDestCount; ++i)
                if (id == kModDestTable[i].id) return true;
            return false;
        };

        beginTest("stepped CS + matrix assignment round-trip through serialise/deserialise");
        {
            VoiceSlot src;
            auto& cs = src.controlSequences[0];
            cs.mode          = ControlSequence::Mode::Stepped;
            cs.polarity      = ControlSequence::Polarity::Unipolar;
            cs.stepValues    = { 10.0f, -20.0f, 30.0f, 0.0f };
            cs.loopMultiplier = 2;

            ModulationAssignment a;
            a.id = "asg0"; a.sourceId = "cs0"; a.destinationId = "filter.cutoff";
            a.depth = 0.5f; a.curve = 0.25f;
            expect(src.modulationMatrix.addAssignment(a), "assignment accepted");

            const auto tree = mu_pp::serialiseModulators(src);
            expect(tree.isValid() && tree.getType() == juce::Identifier("Modulators"), "tree valid");

            VoiceSlot dst;
            mu_pp::clearModulators(dst);
            const auto dropped = mu_pp::deserialiseModulators(tree, dst, {}, destValid);
            expect(dropped.isEmpty(), "nothing dropped");

            const auto& dcs = dst.controlSequences[0];
            expect(dcs.mode == ControlSequence::Mode::Stepped, "mode restored");
            expect(dcs.polarity == ControlSequence::Polarity::Unipolar, "polarity restored");
            expectEquals((int) dcs.stepValues.size(), 4, "step count restored");
            expectWithinAbsoluteError(dcs.stepValues[1], -20.0f, 0.001f);
            expectEquals(dcs.loopMultiplier, 2);

            const auto& asgs = dst.modulationMatrix.getAssignments();
            expectEquals((int) asgs.size(), 1, "one assignment restored");
            if (! asgs.empty())
            {
                expect(asgs[0].destinationId == "filter.cutoff", "dest restored");
                expect(asgs[0].sourceId == "cs0", "source restored");
                expectWithinAbsoluteError(asgs[0].depth, 0.5f, 0.001f);
            }
        }

        beginTest("smooth-mode curve points round-trip");
        {
            VoiceSlot src;
            auto& cs = src.controlSequences[1];
            cs.mode = ControlSequence::Mode::Smooth;
            cs.curvePoints = { { 0.0f,  0.0f, false, 0.0f, 0.0f },
                               { 0.5f,  1.0f, false, 0.0f, 0.0f },
                               { 1.0f, -1.0f, false, 0.0f, 0.0f } };

            const auto tree = mu_pp::serialiseModulators(src);
            VoiceSlot dst;
            mu_pp::clearModulators(dst);
            mu_pp::deserialiseModulators(tree, dst, {}, destValid);

            const auto& dcs = dst.controlSequences[1];
            expect(dcs.mode == ControlSequence::Mode::Smooth, "mode restored");
            expectEquals((int) dcs.curvePoints.size(), 3, "point count restored");
            expectWithinAbsoluteError(dcs.curvePoints[1].x, 0.5f, 0.001f);
            expectWithinAbsoluteError(dcs.curvePoints[1].y, 1.0f, 0.001f);
        }

        beginTest("deserialise drops an assignment to an unknown destination");
        {
            VoiceSlot src;
            ModulationAssignment a;
            a.id = "asg1"; a.sourceId = "cs0"; a.destinationId = "bogus.dest"; a.depth = 0.5f;
            src.modulationMatrix.addAssignment(a);

            const auto tree = mu_pp::serialiseModulators(src);
            VoiceSlot dst;
            mu_pp::clearModulators(dst);
            const auto dropped = mu_pp::deserialiseModulators(tree, dst, {}, destValid);
            expect(! dropped.isEmpty(), "bogus dest reported dropped");
            expectEquals((int) dst.modulationMatrix.getAssignments().size(), 0, "not added");
        }

        beginTest("clearModulators empties step/curve data + assignments");
        {
            VoiceSlot s;
            s.controlSequences[0].stepValues = { 1.0f, 2.0f, 3.0f };
            s.controlSequences[2].curvePoints = { { 0.0f, 0.5f, false, 0.0f, 0.0f } };
            ModulationAssignment a; a.id = "x"; a.sourceId = "cs0"; a.destinationId = "level"; a.depth = 1.0f;
            s.modulationMatrix.addAssignment(a);

            mu_pp::clearModulators(s);
            expect(s.controlSequences[0].stepValues.empty(), "steps cleared");
            expect(s.controlSequences[2].curvePoints.empty(), "points cleared");
            expect(s.modulationMatrix.getAssignments().empty(), "assignments cleared");
        }

        beginTest("deserialise of an invalid/empty tree is a safe no-op");
        {
            VoiceSlot dst;
            const auto dropped = mu_pp::deserialiseModulators(juce::ValueTree{}, dst, {}, destValid);
            expect(dropped.isEmpty(), "no work, no drops");
        }
    }
};

static MuTantPersistTest muTantPersistTest;
