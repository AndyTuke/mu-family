// MIDI control mapping tests: mu-core MidiControlMap (the saved CC / note -> action table, MIDI
// learn) and MidiControlRouter (the audio-thread hand-off: knob coalescing, button presses,
// stripping claimed messages, launch quantise against the resolved transport).

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Control/MidiControlMap.h"
#include "Control/MidiControlRouter.h"

using namespace mu_core;

class MidiControlTest : public juce::UnitTest
{
public:
    MidiControlTest() : juce::UnitTest("MIDI control map / router", "Control") {}

    // Remembers every action it is asked to perform.
    struct Recorder : ControlSink
    {
        std::vector<ControlAction> got;
        bool perform(const ControlAction& a) override { got.push_back(a); return true; }
    };

    static MidiMapping cc(int ch, int num, ControlAction a)
    {
        MidiMapping m; m.isNote = false; m.channel = ch; m.number = num; m.action = a; return m;
    }
    static MidiMapping note(int ch, int num, ControlAction a)
    {
        MidiMapping m; m.isNote = true; m.channel = ch; m.number = num; m.action = a; return m;
    }
    static ControlAction param(const char* id)
    {
        ControlAction a; a.type = ControlActionType::Parameter; a.paramId = id; return a;
    }
    static ControlAction act(ControlActionType t, int layer = 0, Quantise q = Quantise::Default)
    {
        ControlAction a; a.type = t; a.layer = layer; a.quantise = q; return a;
    }

    // A fresh buffer holding one CC press (the router strips what it claims, so never reuse one).
    static juce::MidiBuffer pressOf(int cc, int ch = 1)
    {
        juce::MidiBuffer b;
        b.addEvent(juce::MidiMessage::controllerEvent(ch, cc, 127), 0);
        return b;
    }

    // Feed contiguous playing blocks of `len` beats from `from` and return the start beat of the
    // first block after which the router had handed something over (-1 if none by `to`).
    static double firstFire(MidiControlRouter& router, Recorder& rec, double from, double to, double len)
    {
        for (double start = from; start < to; start += len)
        {
            router.noteBlock(playingBlock(start, len), 4.0);
            juce::MidiBuffer none;
            router.process(none);
            router.drain(rec);
            if (! rec.got.empty()) return start;
        }
        return -1.0;
    }

    // A playing block of `beats` starting at `start`.
    static BlockTransport playingBlock(double start, double beats)
    {
        BlockTransport t;
        t.playing = true; t.startBeat = start; t.blockBeats = beats; t.bpm = 120.0;
        return t;
    }

    void runTest() override
    {
        beginTest("map: add, replace, remove, clear, lookup");
        {
            MidiControlMap map;
            expectEquals(map.find(false, 1, 7), -1);
            expectEquals(map.add(cc(1, 7, param("a"))), 0);
            expectEquals(map.add(cc(2, 7, param("b"))), 1);
            expectEquals(map.find(false, 1, 7), 0);
            expectEquals(map.find(false, 2, 7), 1);
            expectEquals(map.find(true, 1, 7), -1);          // a note is not a CC

            expectEquals(map.add(cc(1, 7, param("c"))), 0);  // same source: replaced
            expectEquals(map.size(), 2);
            expectEquals(map.get(0).action.paramId, juce::String("c"));

            map.remove(0);
            expectEquals(map.size(), 1);
            expectEquals(map.find(false, 1, 7), -1);
            expectEquals(map.find(false, 2, 7), 0);          // the survivor moved down

            expectEquals(map.add(cc(0, 7, param("x"))), -1);                 // channel out of range
            expectEquals(map.add(cc(1, 200, param("x"))), -1);               // number out of range
            expectEquals(map.add(cc(1, 9, ControlAction {})), -1);           // no action
            map.clear();
            expectEquals(map.size(), 0);
            expectEquals(map.find(false, 2, 7), -1);
        }

        beginTest("map: save / load round trip");
        {
            const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("muControlMapTest.json");
            file.deleteFile();
            {
                MidiControlMap map;
                map.setStorageFile(file);
                map.setQuantise(Quantise::Beat);
                auto m = act(ControlActionType::MuteLayer, 3, Quantise::Off);
                map.add(note(10, 36, m));
                map.add(cc(1, 21, param("v0_cutoff")));
            }
            MidiControlMap loaded;
            loaded.setStorageFile(file);
            loaded.load();
            expectEquals(loaded.size(), 2);
            expect(loaded.getQuantise() == Quantise::Beat, "global quantise lost");
            const int i = loaded.find(true, 10, 36);
            expect(i >= 0, "note mapping lost");
            if (i >= 0)
            {
                const auto m = loaded.get(i);
                expect(m.action.type == ControlActionType::MuteLayer, "action type lost");
                expectEquals(m.action.layer, 3);
                expect(m.action.quantise == Quantise::Off, "mapping quantise lost");
                expect(loaded.quantiseOf(i) == Quantise::Off, "audio-side quantise not republished");
            }
            const int j = loaded.find(false, 1, 21);
            expect(j >= 0 && loaded.get(j).action.paramId == "v0_cutoff", "parameter id lost");
            file.deleteFile();
        }

        beginTest("router: a CC sweep coalesces to the latest value; the CC leaves the block");
        {
            MidiControlMap map; map.add(cc(1, 21, param("p")));
            MidiControlRouter router(map);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::controllerEvent(1, 21, 10), 5);
            midi.addEvent(juce::MidiMessage::controllerEvent(1, 21, 127), 9);
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 12);   // unmapped: must stay
            expect(router.process(midi), "a mapped knob should wake the message thread");

            expectEquals(midi.getNumEvents(), 1);
            for (const auto meta : midi)
            {
                expect(meta.getMessage().isNoteOn(), "the note should survive");
                expectEquals(meta.samplePosition, 12);   // and keep its position
            }

            Recorder rec;
            router.drain(rec);
            expectEquals((int) rec.got.size(), 1);       // one write for the whole sweep
            if (! rec.got.empty())
            {
                expectEquals(rec.got[0].paramId, juce::String("p"));
                expectWithinAbsoluteError(rec.got[0].value, 1.0f, 0.001f);
            }
            router.drain(rec);
            expectEquals((int) rec.got.size(), 1);       // nothing new: nothing performed
        }

        beginTest("router: a note mapped to a parameter gives 1 on press, 0 on release");
        {
            MidiControlMap map; map.add(note(1, 40, param("btn")));
            MidiControlRouter router(map);
            Recorder rec;

            juce::MidiBuffer on;  on.addEvent(juce::MidiMessage::noteOn(1, 40, (juce::uint8) 90), 0);
            router.process(on);  router.drain(rec);
            juce::MidiBuffer off; off.addEvent(juce::MidiMessage::noteOff(1, 40), 0);
            router.process(off); router.drain(rec);

            expectEquals((int) rec.got.size(), 2);
            if (rec.got.size() == 2)
            {
                expectWithinAbsoluteError(rec.got[0].value, 1.0f, 0.001f);
                expectWithinAbsoluteError(rec.got[1].value, 0.0f, 0.001f);
            }
        }

        beginTest("router: a button acts on its press, not its release");
        {
            MidiControlMap map; map.add(cc(1, 60, act(ControlActionType::TransportToggle)));
            MidiControlRouter router(map);
            Recorder rec;

            juce::MidiBuffer press;   press.addEvent(juce::MidiMessage::controllerEvent(1, 60, 127), 0);
            juce::MidiBuffer release; release.addEvent(juce::MidiMessage::controllerEvent(1, 60, 0), 0);
            router.process(press);   router.drain(rec);
            router.process(release); router.drain(rec);
            expectEquals((int) rec.got.size(), 1);
            expectEquals(press.getNumEvents() + release.getNumEvents(), 0);   // both claimed
        }

        beginTest("router: MIDI learn claims the first message and reports it once");
        {
            MidiControlMap map;
            MidiControlRouter router(map);
            map.armLearn();
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::controllerEvent(3, 74, 64), 0);
            midi.addEvent(juce::MidiMessage::controllerEvent(3, 75, 64), 1);
            router.process(midi);
            expectEquals(midi.getNumEvents(), 0);                // learning swallows controller traffic

            bool isNote = true; int ch = 0, num = 0;
            expect(map.takeLearned(isNote, ch, num), "nothing learned");
            expect(! isNote, "learned a CC, not a note");
            expectEquals(ch, 3);
            expectEquals(num, 74);                               // the first one, not the second
            expect(! map.takeLearned(isNote, ch, num), "reported twice");
            expect(! map.isLearning(), "learn should disarm itself");
        }

        beginTest("router: mute on the bar waits for the boundary; stopped or Off fires at once");
        {
            MidiControlMap map;
            map.setQuantise(Quantise::Bar);
            map.add(cc(1, 1, act(ControlActionType::MuteLayer, 2)));
            MidiControlRouter router(map);
            Recorder rec;

            // Playing, 4 beats a bar: the last block ended at beat 5.9, so the press waits for beat 8
            // and is handed over in the block that leads into it - never earlier, never after.
            router.noteBlock(playingBlock(5.8, 0.1), 4.0);
            auto first = pressOf(1);
            router.process(first); router.drain(rec);
            expectEquals((int) rec.got.size(), 0);

            const double at = firstFire(router, rec, 5.9, 12.0, 0.1);
            expect(at >= 7.6 && at <= 8.0, "fired at beat " + juce::String(at) + ", expected just before the bar at 8");
            if (! rec.got.empty()) expectEquals(rec.got[0].layer, 2);

            // Stopped: immediate.
            rec.got.clear();
            BlockTransport stopped; stopped.playing = false;
            router.noteBlock(stopped, 4.0);
            auto again = pressOf(1);
            router.process(again); router.drain(rec);
            expectEquals((int) rec.got.size(), 1);

            // Off for this mapping: immediate even while playing.
            rec.got.clear();
            map.add(cc(1, 2, act(ControlActionType::MuteLayer, 1, Quantise::Off)));
            router.noteBlock(playingBlock(1.0, 0.1), 4.0);
            juce::MidiBuffer off; off.addEvent(juce::MidiMessage::controllerEvent(1, 2, 127), 0);
            router.process(off); router.drain(rec);
            expectEquals((int) rec.got.size(), 1);
        }

        beginTest("router: beat quantise waits for the next beat; the global setting is the default");
        {
            MidiControlMap map;
            map.setQuantise(Quantise::Beat);                      // the global setting
            map.add(cc(1, 1, act(ControlActionType::SoloLayer, 0)));
            MidiControlRouter router(map);
            Recorder rec;

            // The last block ended at 2.2; the next beat is 3.
            router.noteBlock(playingBlock(2.1, 0.1), 4.0);
            auto press = pressOf(1);
            router.process(press); router.drain(rec);
            expectEquals((int) rec.got.size(), 0);
            const double at = firstFire(router, rec, 2.2, 6.0, 0.1);
            expect(at >= 2.6 && at <= 3.0, "fired at beat " + juce::String(at) + ", expected just before beat 3");

            // Again, with longer blocks: the last ended at 6.2, the next beat is 7.
            rec.got.clear();
            router.noteBlock(playingBlock(5.9, 0.3), 4.0);
            auto p2 = pressOf(1);
            router.process(p2); router.drain(rec);
            expectEquals((int) rec.got.size(), 0);
            const double at2 = firstFire(router, rec, 6.2, 9.0, 0.3);
            expect(at2 >= 6.4 && at2 <= 7.0, "fired at beat " + juce::String(at2) + ", expected just before beat 7");
        }

        beginTest("router: actions that change nothing audible never wait");
        {
            MidiControlMap map;
            map.setQuantise(Quantise::Bar);
            map.add(cc(1, 1, act(ControlActionType::TransportPlay)));
            map.add(cc(1, 2, act(ControlActionType::TransportStop)));
            MidiControlRouter router(map);
            Recorder rec;
            router.noteBlock(playingBlock(5.8, 0.1), 4.0);

            juce::MidiBuffer play; play.addEvent(juce::MidiMessage::controllerEvent(1, 1, 127), 0);
            router.process(play); router.drain(rec);
            expectEquals((int) rec.got.size(), 1);                // Play is immediate

            juce::MidiBuffer stop; stop.addEvent(juce::MidiMessage::controllerEvent(1, 2, 127), 0);
            router.process(stop); router.drain(rec);
            expectEquals((int) rec.got.size(), 1);                // Stop waits for the bar
        }

        beginTest("router: a waiting action fires when playback stops or the beat jumps back");
        {
            MidiControlMap map;
            map.setQuantise(Quantise::Bar);
            map.add(cc(1, 1, act(ControlActionType::MuteLayer, 0)));
            MidiControlRouter router(map);
            Recorder rec;
            juce::MidiBuffer none;

            router.noteBlock(playingBlock(5.8, 0.1), 4.0);
            auto press = pressOf(1);
            router.process(press); router.drain(rec);
            expectEquals((int) rec.got.size(), 0);

            BlockTransport stopped; stopped.playing = false;
            router.noteBlock(stopped, 4.0);
            router.process(none); router.drain(rec);
            expectEquals((int) rec.got.size(), 1);                // stopped: no longer waits

            rec.got.clear();
            router.noteBlock(playingBlock(5.8, 0.1), 4.0);
            auto press2 = pressOf(1);
            router.process(press2); router.drain(rec);
            router.noteBlock(playingBlock(0.0, 0.1), 4.0);        // looped back past the boundary
            router.process(none); router.drain(rec);
            expectEquals((int) rec.got.size(), 1);
        }

        beginTest("router: more presses than the queue holds are dropped, not overflowed");
        {
            MidiControlMap map; map.add(cc(1, 1, act(ControlActionType::TransportToggle)));
            MidiControlRouter router(map);
            juce::MidiBuffer many;
            for (int i = 0; i < 200; ++i) many.addEvent(juce::MidiMessage::controllerEvent(1, 1, 127), i);
            router.process(many);
            Recorder rec;
            router.drain(rec);
            expect((int) rec.got.size() > 0 && (int) rec.got.size() < 200, "queue should cap, not grow or lose everything");
        }

        beginTest("router: a queued action is dropped when the table is edited before it is performed");
        {
            MidiControlMap map;
            map.add(cc(1, 1, act(ControlActionType::TransportToggle)));
            map.add(cc(1, 2, act(ControlActionType::MuteLayer, 5)));
            MidiControlRouter router(map);
            Recorder rec;

            auto press = pressOf(2);                              // the second mapping (a mute)
            router.noteBlock(BlockTransport {}, 4.0);             // stopped: queued at once
            router.process(press);
            map.remove(0);                                        // indices shift under the queued event
            router.drain(rec);
            expectEquals((int) rec.got.size(), 0);                // not performed as some other mapping
        }

        beginTest("router: a waiting action is released by a loop or relocate");
        {
            MidiControlMap map;
            map.setQuantise(Quantise::Bar);
            map.add(cc(1, 1, act(ControlActionType::MuteLayer, 0)));
            MidiControlRouter router(map);
            Recorder rec;

            router.noteBlock(playingBlock(5.8, 0.1), 4.0);
            auto press = pressOf(1);
            router.process(press); router.drain(rec);
            router.noteBlock(playingBlock(5.9, 0.1), 4.0);        // contiguous: keeps waiting
            juce::MidiBuffer none; router.process(none); router.drain(rec);
            expectEquals((int) rec.got.size(), 0);

            router.noteBlock(playingBlock(0.0, 0.1), 4.0);        // the host looped to the start
            router.process(none); router.drain(rec);
            expectEquals((int) rec.got.size(), 1);
        }

        beginTest("router: learn lets a release through and claims the first press");
        {
            MidiControlMap map;
            MidiControlRouter router(map);
            map.armLearn();
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOff(1, 50), 0);              // a held note ending
            midi.addEvent(juce::MidiMessage::noteOn(1, 51, (juce::uint8) 80), 1);
            router.process(midi);
            expectEquals(midi.getNumEvents(), 1);                              // the release stays in the block
            bool isNote = false; int ch = 0, num = 0;
            expect(map.takeLearned(isNote, ch, num) && isNote && num == 51, "should learn the press, not the release");
        }

        beginTest("action names round trip and unknown names are none");
        {
            for (auto t : { ControlActionType::Parameter, ControlActionType::TransportToggle, ControlActionType::TransportPlay,
                            ControlActionType::TransportStop, ControlActionType::MuteLayer, ControlActionType::SoloLayer,
                            ControlActionType::SelectLayer, ControlActionType::LaunchClip, ControlActionType::PresetNext,
                            ControlActionType::PresetPrev, ControlActionType::Panic })
                expect(actionFromName(actionName(t)) == t, juce::String("action name did not round trip: ") + actionName(t));
            expect(actionFromName("nonsense") == ControlActionType::None, "unknown name should be none");
        }
    }
};

static MidiControlTest midiControlTest;
