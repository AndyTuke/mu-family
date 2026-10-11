// A hot-swap commit lands on the message thread some time after the audio thread flags the loop
// boundary (up to about 10 ms from the swap poll), so the old pattern may play a step or two
// first. These tests commit a swap the way HotSwapStager::installRhythm does (new rhythm, then
// requestSwapStart with the boundary step) at a chosen delay and check where the new pattern starts.

#include <juce_core/juce_core.h>
#include <atomic>
#include <thread>
#include "Sequencer/SequencerEngine.h"

using namespace mu_clid;

class SwapCommitTimingTest : public juce::UnitTest
{
public:
    SwapCommitTimingTest() : juce::UnitTest("Hot-swap commit timing", "SequencerEngine") {}

    // A `steps`-step rhythm with `hits` hits and nothing from the B generator.
    static Rhythm rhythmOf(int hits, int steps = 8)
    {
        Rhythm r;
        r.genA.steps = steps; r.genA.hits = hits; r.genA.rotate = 0;
        r.genB.steps = steps; r.genB.hits = 0;    r.genB.rotate = 0;
        return r;
    }

    // The message-thread half of a swap: install `next` the way installRhythm does, its step 0
    // falling on `boundaryStep`.
    static void commit(SequencerEngine& e, const Rhythm& next, int boundaryStep)
    {
        e.getRhythm(0) = next;
        e.requestSwapStart(0, boundaryStep);
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
            commit(e, rhythmOf(8), 8);                     // new: every step hits
            expect(fired(e.processBlock(beat(8))), "first step after the commit should fire");
        }

        beginTest("A swap committed one step late: the old pattern plays the extra step, the new one starts next");
        {
            SequencerEngine e;
            e.addRhythm(rhythmOf(1));
            for (int s = 0; s < 8; ++s) e.processBlock(beat(s));
            expect(fired(e.processBlock(beat(8))), "old pattern's step 0 plays before the commit lands");
            commit(e, rhythmOf(8), 8);
            expect(fired(e.processBlock(beat(9))), "first step after the commit should fire the new pattern");
            expect(fired(e.processBlock(beat(10))), "and it keeps going");
        }

        beginTest("The old pattern is silent from the commit on");
        {
            SequencerEngine e;
            e.addRhythm(rhythmOf(8));                      // old: every step hits
            for (int s = 0; s < 8; ++s) e.processBlock(beat(s));
            commit(e, rhythmOf(0), 8);                     // new: silent
            for (int s = 8; s < 16; ++s)
                expect(! fired(e.processBlock(beat(s))), "step " + juce::String(s) + " must not fire the old pattern");
        }

        beginTest("A late commit keeps the new pattern aligned to the boundary: its step 0 comes a pass later");
        {
            SequencerEngine e;
            e.addRhythm(rhythmOf(8));
            for (int s = 0; s < 8; ++s) e.processBlock(beat(s));
            e.processBlock(beat(8));
            e.processBlock(beat(9));                       // two extra old steps
            commit(e, rhythmOf(1), 8);                     // new: a hit on step 0 only
            int firstFire = -1;
            for (int s = 10; s < 30 && firstFire < 0; ++s)
                if (fired(e.processBlock(beat(s)))) firstFire = s;
            expectEquals(firstFire, 16);
        }

        beginTest("A different pattern length starts its step 0 on the boundary step");
        {
            SequencerEngine e;
            e.addRhythm(rhythmOf(1, 16));                  // old: 16 steps, a hit on step 0
            for (int s = 0; s < 16; ++s) e.processBlock(beat(s));
            commit(e, rhythmOf(1, 12), 16);                // new: 12 steps, a hit on step 0
            expect(fired(e.processBlock(beat(16))), "new step 0 plays on the boundary step");
            int next = -1;
            for (int s = 17; s < 60 && next < 0; ++s)
                if (fired(e.processBlock(beat(s)))) next = s;
            expectEquals(next, 28);                        // 16 + 12, not the absolute step modulo 12 (24)
        }

        beginTest("A commit landing in a step already played adds no extra hit");
        {
            SequencerEngine e;
            e.addRhythm(rhythmOf(8));                      // old: every step hits
            for (int s = 0; s <= 8; ++s) e.processBlock(beat(s));
            commit(e, rhythmOf(8), 8);                     // new: every step hits, landing mid step 8
            expect(! fired(e.processBlock(beat(8) + 0.05)), "same step: no second hit");
            expect(fired(e.processBlock(beat(9))), "the next step plays the new pattern");
        }

        beginTest("A commit delayed more than a whole loop still lines up with the boundary");
        {
            SequencerEngine e;
            e.addRhythm(rhythmOf(8));
            for (int s = 0; s < 8 + 20; ++s) e.processBlock(beat(s));   // 20 extra old steps
            commit(e, rhythmOf(1, 12), 8);                 // new: 12 steps, a hit on step 0
            int firstFire = -1;
            for (int s = 28; s < 80 && firstFire < 0; ++s)
                if (fired(e.processBlock(beat(s)))) firstFire = s;
            expectEquals(firstFire, 32);                   // 8 + 2 * 12
        }

        beginTest("A swap flagged before the step counter restarted starts from step 0");
        {
            SequencerEngine e;
            e.addRhythm(rhythmOf(1, 16));
            for (int s = 0; s < 10; ++s) e.processBlock(beat(s));
            const auto staleEpoch = e.getFrameEpoch();
            e.resetWrapDetector();                         // transport restart: the counter starts over
            e.processBlock(beat(0));
            e.getRhythm(0) = rhythmOf(1, 12);
            e.requestSwapStart(0, 8, staleEpoch);          // origin from the old run is dropped
            int firstFire = -1;
            for (int s = 1; s < 40 && firstFire < 0; ++s)
                if (fired(e.processBlock(beat(s)))) firstFire = s;
            expectEquals(firstFire, 12);
        }

        beginTest("Swap requests racing the audio thread are safe");
        {
            // The audio thread runs a fixed number of blocks while this thread requests swaps
            // until it is done; the lock-free hand-over must neither crash nor corrupt a pattern.
            SequencerEngine e;
            e.addRhythm(rhythmOf(4, 16));
            std::atomic<bool> done { false };
            int fires = 0;
            std::thread audio([&]
            {
                for (int b = 0; b < 40000; ++b)
                    if (fired(e.processBlock(beat(b / 4)))) ++fires;
                done.store(true);
            });
            int requests = 0;
            while (! done.load())
            {
                e.getRhythm(0) = rhythmOf(4, 12 + (requests % 5));
                e.requestSwapStart(0, requests, e.getFrameEpoch());
                ++requests;
            }
            audio.join();
            expect(fires > 0, "the sequencer kept playing");
            expect(e.getPatternLength(0) >= 12 && e.getPatternLength(0) <= 16);
        }
    }
};

static SwapCommitTimingTest swapCommitTimingTest;
