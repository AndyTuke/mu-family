// A hot-swap commit lands on the message thread some time after the audio thread flags the loop
// boundary (up to about 33 ms from the swap poll), so the old pattern may play a step or two
// first. These tests commit a swap the way HotSwapStager::installRhythm does (new rhythm,
// updatePattern, resetStepTrackingForSwap) at a chosen delay and check where the new pattern starts.

#include <juce_core/juce_core.h>
#include "Sequencer/SequencerEngine.h"

using namespace mu_clid;

class SwapCommitTimingTest : public juce::UnitTest
{
public:
    SwapCommitTimingTest() : juce::UnitTest("Hot-swap commit timing", "SequencerEngine") {}

    // An 8-step rhythm with `hits` hits and nothing from the B generator.
    static Rhythm rhythmOf(int hits)
    {
        Rhythm r;
        r.genA.steps = 8; r.genA.hits = hits; r.genA.rotate = 0;
        r.genB.steps = 8; r.genB.hits = 0;    r.genB.rotate = 0;
        return r;
    }

    // The message-thread half of a swap: install `next` the way installRhythm does.
    static void commit(SequencerEngine& e, const Rhythm& next)
    {
        e.getRhythm(0) = next;
        e.updatePattern(0);
        e.resetStepTrackingForSwap(0);
    }

    static double beat(int step) { return step * SequencerEngine::StepLengthBeats; }
    static bool   fired(const BlockResult& r) { return (r.firedMask & 1) != 0; }

    void runTest() override
    {
        beginTest("A swap committed on the boundary step fires the new pattern at once");
        {
            SequencerEngine e;
            e.addRhythm(rhythmOf(1));                      // old: a hit on step 0 only
            for (int s = 0; s < 8; ++s) e.processBlock(beat(s));
            commit(e, rhythmOf(8));                        // new: every step hits
            expect(fired(e.processBlock(beat(8))), "first step after the commit should fire");
        }

        beginTest("A swap committed one step late: the old pattern plays the extra step, the new one starts next");
        {
            SequencerEngine e;
            e.addRhythm(rhythmOf(1));
            for (int s = 0; s < 8; ++s) e.processBlock(beat(s));
            expect(fired(e.processBlock(beat(8))), "old pattern's step 0 plays before the commit lands");
            commit(e, rhythmOf(8));
            expect(fired(e.processBlock(beat(9))), "first step after the commit should fire the new pattern");
            expect(fired(e.processBlock(beat(10))), "and it keeps going");
        }

        beginTest("The old pattern is silent from the commit on");
        {
            SequencerEngine e;
            e.addRhythm(rhythmOf(8));                      // old: every step hits
            for (int s = 0; s < 8; ++s) e.processBlock(beat(s));
            commit(e, rhythmOf(0));                        // new: silent
            for (int s = 8; s < 16; ++s)
                expect(! fired(e.processBlock(beat(s))), "step " + juce::String(s) + " must not fire the old pattern");
        }

        beginTest("A late commit joins the new pattern at its running step, not at step 0");
        {
            // Recorded behaviour, not a requirement: the pattern position is the absolute step
            // modulo the pattern length, so a commit k steps after the boundary starts the new
            // pattern at its step k and its step 0 first sounds a loop later.
            SequencerEngine e;
            e.addRhythm(rhythmOf(8));
            for (int s = 0; s < 8; ++s) e.processBlock(beat(s));
            e.processBlock(beat(8));
            e.processBlock(beat(9));                       // two extra old steps
            commit(e, rhythmOf(1));                        // new: a hit on step 0 only
            int firstFire = -1;
            for (int s = 10; s < 30 && firstFire < 0; ++s)
                if (fired(e.processBlock(beat(s)))) firstFire = s;
            logMessage("new pattern's step 0 first fired at absolute step " + juce::String(firstFire)
                       + " (boundary was step 8, commit landed before step 10)");
            expect(firstFire >= 0);
        }
    }
};

static SwapCommitTimingTest swapCommitTimingTest;
