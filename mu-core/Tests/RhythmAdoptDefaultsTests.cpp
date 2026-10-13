// Rhythm::adoptDefaults is what resetLayer calls on a Rhythm: every piece of its data is replaced by a
// default Rhythm's, in place, while the layer keeps its name, colour and the address of its control sequences.

#include <juce_core/juce_core.h>
#include "Sequencer/Rhythm.h"

#include <vector>

struct RhythmAdoptDefaultsTests : juce::UnitTest
{
    RhythmAdoptDefaultsTests() : juce::UnitTest("Rhythm::adoptDefaults", "mu-core") {}

    void runTest() override
    {
        using mu_core::Logic;
        using mu_core::OutputMode;
        using mu_core::Rhythm;

        beginTest("every Euclid and output field returns to its default; name and colour stay");
        {
            Rhythm r;
            r.name = "Kick"; r.colourIndex = 5;
            r.genA.steps = 12; r.genA.hits = 7; r.genA.rotate = 3;
            r.genB.steps = 9;  r.genB.hits = 2;
            r.genC.steps = 5;  r.genC.hits = 5;
            r.logic = Logic::XOR;
            r.resetSteps = 24;
            r.patternLegato = true;
            r.outputMode.store((int) OutputMode::Midi);
            r.midiNote.store(60); r.midiChannel.store(4); r.midiVelocity.store(55); r.midiAccent.store(30);
            r.voiceParams.ampLevel = 0.2f;

            Rhythm fresh;
            fresh.name = "Rhythm 9";

            r.adoptDefaults(fresh);

            const Rhythm def;
            expectEquals(r.genA.steps, def.genA.steps); expectEquals(r.genA.hits, def.genA.hits); expectEquals(r.genA.rotate, def.genA.rotate);
            expectEquals(r.genB.steps, def.genB.steps); expectEquals(r.genB.hits, def.genB.hits);
            expectEquals(r.genC.steps, def.genC.steps); expectEquals(r.genC.hits, def.genC.hits);
            expect(r.logic == def.logic);
            expect(! r.resetSteps.has_value());
            expect(! r.patternLegato);
            expectEquals(r.outputMode.load(), def.outputMode.load());
            expectEquals(r.midiNote.load(), def.midiNote.load());
            expectEquals(r.midiChannel.load(), def.midiChannel.load());
            expectEquals(r.midiVelocity.load(), def.midiVelocity.load());
            expectEquals(r.midiAccent.load(), def.midiAccent.load());
            expectEquals(r.voiceParams.ampLevel, def.voiceParams.ampLevel);
            expectEquals(r.name, std::string("Kick"));
            expectEquals(r.colourIndex, 5);
        }

        beginTest("the data that was replaced is left in the other rhythm, to be destroyed after the lock");
        {
            Rhythm r, fresh;
            r.genA.steps = 12; r.genA.hits = 7;
            r.outputMode.store((int) OutputMode::Midi);
            r.adoptDefaults(fresh);
            expectEquals(fresh.genA.steps, 12);
            expectEquals(fresh.genA.hits, 7);
            expectEquals(fresh.outputMode.load(), (int) OutputMode::Midi);
        }

        beginTest("the rhythm's control sequences keep their addresses (an editor holds pointers to them)");
        {
            Rhythm r, fresh;
            std::vector<const void*> before;
            for (auto& cs : r.controlSequences) before.push_back(&cs);
            r.adoptDefaults(fresh);
            expectEquals((int) r.controlSequences.size(), (int) before.size());
            bool same = true;
            for (size_t i = 0; i < before.size(); ++i)
                same = same && (&r.controlSequences[i] == before[i]);
            expect(same);
        }
    }
};

static RhythmAdoptDefaultsTests rhythmAdoptDefaultsTests;
