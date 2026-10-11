// Host-session state: this product's own round trip, and a session saved by mu-Clid Lite (a full
// mu-Clid preset tree plus per-parameter rows) still restoring its rhythm, loop and MIDI note.

#include <juce_audio_processors/juce_audio_processors.h>
#include "Tests/StubProcessor.h"
#include "Persistence/SessionState.h"
#include "Persistence/RhythmPresetHelpers.h"   // writeKindedProperty, to build a Lite-shaped tree

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
        for (int j = 0; j < mu_core::kRhythmParamCount; ++j)
        {
            const auto& def = mu_core::kRhythmParamDefs[j];
            const juce::String s(def.suffix);
            float v;
            if (s == "stepsA")  v = 12.0f;
            else if (s == "hitsA")   v = 5.0f;
            else if (s == "rotA")    v = -3.0f;
            else if (s == "logic")   v = 2.0f;   // XOR
            else if (s == "patLeg")  v = 1.0f;
            else if (s == "insModeB") v = 1.0f;
            else continue;
            mu_core::writeKindedProperty(rhythm, s, v, def.kind, def.algorithmNames);
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

    // The default (de-normalised) value of a parameter in the product's layout.
    static float defaultOf(const juce::String& id)
    {
        tests::StubProcessor fresh;
        return fresh.actual(id);
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

        beginTest("A Lite session restored onto a non-default instance resets what it does not carry");
        {
            tests::StubProcessor p;
            p.setActual("r0_stepsB", 9.0f);
            p.setActual("r0_hitsB", 4.0f);
            p.setActual("lite_midiNote", 100.0f);
            p.setActual("mstrLoop", 7.0f);
            p.setActual("r0_stepsA", 30.0f);

            juce::String error;
            expect(session::restoreFromXmlString(liteSession().toXmlString(), p.apvts, error), error);
            expectEquals(p.actual("r0_stepsA"), 12.0f);        // carried by the session
            expectEquals(p.actual("lite_midiNote"), 40.0f);    // carried by a row
            expectEquals(p.actual("mstrLoop"), 2.0f);
            expectEquals(p.actual("r0_stepsB"), defaultOf("r0_stepsB"));   // not carried: back to the default
            expectEquals(p.actual("r0_hitsB"), defaultOf("r0_hitsB"));
        }

        beginTest("A Lite session with no keys returns every parameter to its default");
        {
            tests::StubProcessor p;
            p.setActual("r0_stepsA", 30.0f);
            p.setActual("lite_midiNote", 100.0f);
            p.setActual("lite_accentAmt", 80.0f);
            p.setActual("mstrLoop", 7.0f);

            juce::String error;
            expect(session::restoreFromXmlString("<MuClidPreset/>", p.apvts, error), error);
            for (const char* id : { "r0_stepsA", "lite_midiNote", "lite_accentAmt", "mstrLoop" })
                expectEquals(p.actual(id), defaultOf(id), id);
        }

        beginTest("A parameter tree missing keys restores them to their defaults");
        {
            tests::StubProcessor a, b;
            a.setActual("r0_hitsA", 7.0f);
            auto tree = a.apvts.copyState();
            for (int i = tree.getNumChildren(); --i >= 0;)
                if (tree.getChild(i).getProperty("id").toString() == "r0_stepsB")
                    tree.removeChild(i, nullptr);

            b.setActual("r0_stepsB", 9.0f);
            b.setActual("lite_midiNote", 100.0f);
            juce::String error;
            expect(session::restoreFromXmlString(tree.toXmlString(), b.apvts, error), error);
            expectEquals(b.actual("r0_hitsA"), 7.0f);
            expectEquals(b.actual("r0_stepsB"), defaultOf("r0_stepsB"));   // the tree lacked it
        }

        beginTest("Parameter nodes this product has no parameter for are dropped from the state");
        {
            tests::StubProcessor a, b;
            auto tree = a.apvts.copyState();
            juce::ValueTree stale("PARAM");
            stale.setProperty("id", "ch0_lvl", nullptr);
            stale.setProperty("value", 0.5f, nullptr);
            tree.appendChild(stale, nullptr);

            juce::String error;
            expect(session::restoreFromXmlString(tree.toXmlString(), b.apvts, error), error);
            for (int i = 0; i < b.apvts.state.getNumChildren(); ++i)
                expect(b.apvts.state.getChild(i).getProperty("id").toString() != "ch0_lvl", "the old node was kept");
        }

        beginTest("An old state's rhythm name and colour are handed back");
        {
            tests::StubProcessor p;
            auto lite = liteSession();
            lite.setProperty("r0_name", "Snare", nullptr);
            lite.setProperty("r0_colour", 5, nullptr);
            session::Identity identity;
            juce::String error;
            expect(session::restoreFromXmlString(lite.toXmlString(), p.apvts, error, &identity), error);
            expectEquals(identity.name, juce::String("Snare"));
            expectEquals(identity.colourIndex, 5);

            auto dump = p.apvts.copyState();
            dump.setProperty("r0_name", "Hat", nullptr);
            session::Identity fromDump;
            expect(session::restoreFromXmlString(dump.toXmlString(), p.apvts, error, &fromDump), error);
            expectEquals(fromDump.name, juce::String("Hat"));
            expectEquals(fromDump.colourIndex, -1);   // none carried
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
