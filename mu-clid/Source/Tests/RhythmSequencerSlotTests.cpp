// The sequencer binds rhythms the product owns and keeps only per-slot playing state of its own.
// These tests cover that split: binding, a grown slot starting clean, and the slot state (phase, step
// history, a waiting swap start) following its rhythm when the product moves it with removeSlotState /
// swapSlotState.

#include <juce_core/juce_core.h>
#include "Sequencer/GeneratorAliases.h"
#include "SequencerTestHarness.h"

using namespace mu_clid;

class RhythmSequencerSlotTest : public juce::UnitTest
{
public:
    RhythmSequencerSlotTest() : juce::UnitTest("Sequencer slot binding and slot state", "RhythmSequencer") {}

    static Rhythm rhythmOf(int hits, int steps = 8)
    {
        Rhythm r;
        r.genA.steps = steps; r.genA.hits = hits; r.genA.rotate = 0;
        r.genB.steps = steps; r.genB.hits = 0;    r.genB.rotate = 0;
        return r;
    }

    // A `steps`-step rhythm with one hit, rotated so that hit sits on step 0.
    static Rhythm hitOnZero(int steps = 8)
    {
        for (int rot = 0; rot < steps; ++rot)
        {
            Rhythm r = rhythmOf(1, steps);
            r.genA.rotate = rot;
            const auto pat = r.getCombinedPattern();
            if ((int) pat.size() == steps && pat[0]) return r;
        }
        return rhythmOf(1, steps);
    }

    static double beat(int step) { return step * RhythmSequencer::StepLengthBeats; }
    static bool   fired(const BlockResult& r, int slot) { return (r.firedMask & (1 << slot)) != 0; }

    void runTest() override
    {
        beginTest("A bound rhythm is the one the sequencer reads and writes");
        {
            Rhythm a = rhythmOf(2, 8);
            Rhythm b = rhythmOf(3, 12);
            RhythmSequencer seq;
            seq.bindRhythm(0, &a);
            seq.setNumRhythms(1);
            expect(&seq.getRhythm(0) == &a, "getRhythm returns the bound object, not a copy");
            seq.updatePattern(0);
            seq.processBlock(0.0);                        // the audio thread snapshots the cached pattern
            expectEquals(seq.getPatternLength(0), 8);

            seq.bindRhythm(0, &b);                       // the product moved its rhythm: rebind
            expect(&seq.getRhythm(0) == &b);
            seq.updatePattern(0);
            seq.processBlock(RhythmSequencer::StepLengthBeats);
            expectEquals(seq.getPatternLength(0), 12, "the new binding is what the pattern is built from");

            seq.bindRhythm(0, nullptr);
            seq.updatePattern(0);                         // unbound: nothing to build, and no crash
            seq.processBlock(2 * RhythmSequencer::StepLengthBeats);
            expectEquals(seq.getPatternLength(0), 12);
        }

        beginTest("A slot that grows starts clean, and a slot that stops being live is cleared");
        {
            TestSequencer e;
            e.addRhythm(hitOnZero());
            e.addRhythm(hitOnZero());
            e.addRhythm(hitOnZero());
            for (int s = 0; s < 6; ++s) e.processBlock(beat(s));
            expect(e.getPatternLength(2) > 0 && e.getLastStepIndex(2) >= 0, "slot 2 has played");

            e.setNumRhythms(1);
            expectEquals(e.getNumRhythms(), 1);
            expectEquals(e.getPatternLength(1), 0, "a dropped slot keeps no pattern");
            expectEquals(e.getLastStepIndex(2), -1, "and no step history");

            e.setNumRhythms(3);
            expectEquals(e.getNumRhythms(), 3);
            expectEquals(e.getPatternLength(2), 0, "a grown slot has no pattern until one is built");
            expectEquals(e.getLastStepIndex(2), -1, "and starts on the cold-start sentinel");
            expectEquals(e.getLastAccentStepIndex(2), 0);
        }

        beginTest("removeSlotState carries phase and step history down with the rhythm");
        {
            TestSequencer e;
            e.addRhythm(rhythmOf(0, 8));                  // slot 0: silent, removed below
            e.addRhythm(hitOnZero());                      // slot 1: one hit on its step 0
            for (int s = 0; s < 10; ++s) e.processBlock(beat(s));
            e.requestSwapStart(1, 3);                      // its step 0 now falls on effective steps 3, 11, ...
            e.processBlock(beat(10));                      // the audio thread adopts the origin
            e.removeRhythm(0);                             // the hit pattern and its state shift to slot 0
            expectEquals(e.getNumRhythms(), 1);
            const auto before = e.processBlock(beat(10) + 0.05);
            expect(! fired(before, 0), "the rest of step 10 does not replay");
            expect(fired(e.processBlock(beat(11)), 0), "the moved pattern keeps its phase: step 0 is effective step 11");
            expectEquals(e.getPatternLength(1), 0, "the vacated top slot is empty");
        }

        beginTest("removeSlotState carries a swap start that is still waiting");
        {
            TestSequencer e;
            e.addRhythm(rhythmOf(0, 8));
            e.addRhythm(hitOnZero());
            for (int s = 0; s < 10; ++s) e.processBlock(beat(s));
            e.requestSwapStart(1, 3);                      // waiting: the audio thread has not seen it yet
            e.removeRhythm(0);
            expect(! fired(e.processBlock(beat(10)), 0), "effective step 10 is its step 7");
            expect(fired(e.processBlock(beat(11)), 0), "the start origin travelled with the rhythm");
        }

        beginTest("swapSlotState carries phase with the rhythm");
        {
            TestSequencer e;
            e.addRhythm(rhythmOf(0, 8));                   // slot 0: silent
            e.addRhythm(hitOnZero());                      // slot 1: one hit on its step 0
            for (int s = 0; s < 10; ++s) e.processBlock(beat(s));
            e.requestSwapStart(1, 3);
            e.processBlock(beat(10));
            e.swapRhythmSlots(0, 1);                       // the hit pattern is now slot 0
            e.processBlock(beat(10) + 0.05);
            const auto r = e.processBlock(beat(11));
            expect(fired(r, 0), "the hit pattern now in slot 0 keeps its phase");
            expect(! fired(r, 1), "the silent pattern in slot 1 stays silent");
        }

        beginTest("A removed slot's pending reorder partner is renumbered");
        {
            TestSequencer e;
            e.addRhythm(rhythmOf(0, 8));
            e.addRhythm(rhythmOf(0, 8));
            e.addRhythm(hitOnZero());
            for (int s = 0; s < 10; ++s) e.processBlock(beat(s));
            e.requestSwapStart(2, 3);
            e.processBlock(beat(10));
            e.swapRhythmSlots(1, 2);                       // hit pattern to slot 1, adopting slot 2's phase
            e.removeRhythm(0);                             // and on to slot 0 before the audio thread looks
            e.processBlock(beat(10) + 0.05);
            expect(fired(e.processBlock(beat(11)), 0), "phase followed the pattern through both moves");
        }
    }
};

static RhythmSequencerSlotTest rhythmSequencerSlotTest;
