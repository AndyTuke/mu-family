// Host-session state: this product's own round trip, and a session saved by mu-Clid Lite (a full
// mu-Clid preset tree plus per-parameter rows) still restoring its rhythm, loop and MIDI note.

#include <juce_audio_processors/juce_audio_processors.h>
#include "Tests/StubProcessor.h"
#include "Persistence/SessionState.h"
#include "Persistence/PresetHelpers.h"   // mu-Clid: writeKindedProperty, to build a Lite-shaped tree

using namespace mu_clid_midi;

class SessionStateTest : public juce::UnitTest
{
public:
    SessionStateTest() : juce::UnitTest("mu-Clid MIDI session state", "MidiProduct") {}

    // A tree shaped like the session mu-Clid Lite wrote: Rhythm child, GlobalState, SessionParams rows.
    static juce::ValueTree liteSession()
    {
        juce::ValueTree root("MuClidPreset");

        juce::ValueTree rhythm("Rhythm");
        for (int j = 0; j < mu_clid::kRhythmParamCount; ++j)
        {
            const auto& def = mu_clid::kRhythmParamDefs[j];
            const juce::String s(def.suffix);
            float v;
            if (s == "stepsA")  v = 12.0f;
            else if (s == "hitsA")   v = 5.0f;
            else if (s == "rotA")    v = -3.0f;
            else if (s == "logic")   v = 2.0f;   // XOR
            else if (s == "patLeg")  v = 1.0f;
            else if (s == "insModeB") v = 1.0f;
            else continue;
            mu_clid::writeKindedProperty(rhythm, s, v, def.kind, def.algorithmNames);
        }
        root.addChild(rhythm, -1, nullptr);

        juce::ValueTree globals("GlobalState");
        globals.setProperty("mstrLoop", 2, nullptr);
        root.addChild(globals, -1, nullptr);

        juce::ValueTree rows("SessionParams");
        for (auto [id, x] : { std::pair<const char*, float>{ "lite_midiNote", 40.0f },
                              std::pair<const char*, float>{ "lite_accentAmt", 75.0f },
                              std::pair<const char*, float>{ "ch0_lvl", 0.5f } })   // a row this product has no parameter for
        {
            juce::ValueTree row("p");
            row.setProperty("id", id, nullptr);
            row.setProperty("x", x, nullptr);
            rows.appendChild(row, nullptr);
        }
        root.addChild(rows, -1, nullptr);
        return root;
    }

    void runTest() override
    {
        beginTest("A session round-trips through text");
        {
            tests::StubProcessor a, b;
            a.setActual("r0_stepsA", 13.0f);
            a.setActual("r0_hitsA", 6.0f);
            a.setActual("lite_midiNote", 48.0f);
            a.setActual("lite_accentAmt", 30.0f);
            a.setActual("mstrLoop", 3.0f);

            juce::String error;
            expect(session::restoreFromXmlString(session::toXmlString(a.apvts), b.apvts, error), error);
            expectEquals(b.actual("r0_stepsA"), 13.0f);
            expectEquals(b.actual("r0_hitsA"), 6.0f);
            expectEquals(b.actual("lite_midiNote"), 48.0f);
            expectWithinAbsoluteError(b.actual("lite_accentAmt"), 30.0f, 0.01f);
            expectEquals(b.actual("mstrLoop"), 3.0f);
        }

        beginTest("A session mu-Clid Lite saved still restores");
        {
            tests::StubProcessor p;
            juce::String error;
            expect(session::restoreFromXmlString(liteSession().toXmlString(), p.apvts, error), error);
            expectEquals(p.actual("r0_stepsA"), 12.0f);
            expectEquals(p.actual("r0_hitsA"), 5.0f);
            expectEquals(p.actual("r0_rotA"), -3.0f);
            expectEquals(p.actual("r0_logic"), 2.0f);
            expectEquals(p.actual("r0_patLeg"), 1.0f);
            expectEquals(p.actual("r0_insModeB"), 1.0f);
            expectEquals(p.actual("mstrLoop"), 2.0f);
            expectEquals(p.actual("lite_midiNote"), 40.0f);
            expectWithinAbsoluteError(p.actual("lite_accentAmt"), 75.0f, 0.01f);
        }

        beginTest("An older host-state dump (the parameter tree) restores");
        {
            tests::StubProcessor a, b;
            a.setActual("r0_stepsB", 9.0f);
            auto tree = a.apvts.copyState();
            tree.setProperty("numRhythms", 1, nullptr);   // the old dump carried extra root properties
            juce::String error;
            expect(session::restoreFromXmlString(tree.toXmlString(), b.apvts, error), error);
            expectEquals(b.actual("r0_stepsB"), 9.0f);
        }

        beginTest("Unreadable state is refused with a message");
        {
            tests::StubProcessor p;
            juce::String error;
            expect(! session::restoreFromXmlString("not xml", p.apvts, error));
            expect(error.isNotEmpty());
            error.clear();
            expect(! session::restoreFromXmlString("<Other/>", p.apvts, error));
            expect(error.isNotEmpty());
            expectEquals(p.actual("r0_stepsA"), 8.0f);   // untouched
        }
    }
};

static SessionStateTest sessionStateTest;
