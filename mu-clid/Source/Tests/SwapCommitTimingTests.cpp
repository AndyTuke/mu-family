// A hot-swap commit lands on the message thread some time after the audio thread flags the loop
// boundary (up to about 10 ms from the swap poll), so the old pattern may play a step or two
// first. These tests commit a swap the way HotSwapStager::installRhythm does (new rhythm, then
// requestSwapStart with the boundary step) at a chosen delay and check where the new pattern starts.

#include <juce_core/juce_core.h>
#include <atomic>
#include <thread>
#include "Sequencer/SequencerEngine.h"
#include "Plugin/HotSwap.h"

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

    // A `steps`-step rhythm with one hit, rotated so that hit sits on `pos` (pos 0 = a downbeat).
    static Rhythm singleHitAt(int pos, int steps)
    {
        for (int rot = 0; rot < steps; ++rot)
        {
            Rhythm r = rhythmOf(1, steps);
            r.genA.rotate = rot;
            const auto pat = r.getCombinedPattern();
            if ((int) pat.size() == steps && pat[(size_t) pos]) return r;
        }
        return rhythmOf(1, steps);
    }

    // `hits` hits in `steps` steps whose step 0 is a rest.
    static Rhythm restOnZero(int hits, int steps)
    {
        for (int rot = 0; rot < steps; ++rot)
        {
            Rhythm r = rhythmOf(hits, steps);
            r.genA.rotate = rot;
            const auto pat = r.getCombinedPattern();
            if (! pat[0]) return r;
        }
        return rhythmOf(hits, steps);
    }

    // Plays the blocks of step `step` (the block at its start, then one commit delay in): returns
    // how many of them fired. The commit lands between the two when `commitNext` is given.
    int fireCount(SequencerEngine& e, int step, double delayBeats, const Rhythm* commitNext, int boundaryStep)
    {
        int n = fired(e.processBlock(beat(step))) ? 1 : 0;
        if (commitNext != nullptr) commit(e, *commitNext, boundaryStep);
        for (double off = delayBeats; off < 0.24; off += 0.05)
            n += fired(e.processBlock(beat(step) + off)) ? 1 : 0;
        return n;
    }

    void runBoundaryCommit(int masterLoop, double bpm)
    {
        // A commit lands about 10 ms after the boundary; in beats that depends on the tempo.
        const double delayBeats = 0.010 * bpm / 60.0;
        const int boundary = masterLoop > 0 ? 0 : 8;
        const int firstStep = 8;

        const auto setup = [&](SequencerEngine& e, const Rhythm& old)
        {
            e.addRhythm(old);
            if (masterLoop > 0) e.setMasterLoopSteps(masterLoop);
            for (int s = 0; s < firstStep; ++s) e.processBlock(beat(s));
        };

        {   // old rest, new hit on step 0: the new downbeat must sound exactly once
            SequencerEngine e;
            setup(e, restOnZero(3, 8));
            const auto next = rhythmOf(4, 8);
            expectEquals(fireCount(e, firstStep, delayBeats, &next, boundary), 1,
                         "old rest / new hit on step 0 at " + juce::String(bpm) + " bpm loop " + juce::String(masterLoop));
        }
        {   // old hit, new hit on step 0: absorbed, so the step sounds once (no flam)
            SequencerEngine e;
            setup(e, rhythmOf(4, 8));
            const auto next = rhythmOf(2, 8);
            expectEquals(fireCount(e, firstStep, delayBeats, &next, boundary), 1,
                         "old hit / new hit on step 0 at " + juce::String(bpm) + " bpm loop " + juce::String(masterLoop));
        }
    }

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

        beginTest("A commit in the boundary step: new downbeat sounds exactly once whether the old step was a rest or a hit");
        for (int loop : { 0, 8 })
            for (double bpm : { 120.0, 200.0 })
                runBoundaryCommit(loop, bpm);

        beginTest("A load while stopped cold-starts: step 0 sounds on play");
        {
            SequencerEngine e;
            e.addRhythm(rhythmOf(1));
            expect(fired(e.processBlock(beat(0))), "old pattern plays step 0");   // then the transport stops
            commit(e, rhythmOf(8), 0);                     // pattern loaded while stopped
            e.resetWrapDetector();                         // play pressed
            expect(fired(e.processBlock(beat(0))), "step 0 must sound after the load");
        }

        beginTest("A master wrap after an off-phase own-loop swap adds no extra hit");
        {
            SequencerEngine e;
            e.addRhythm(rhythmOf(8));
            e.setMasterLoopSteps(16);
            for (int s = 0; s < 10; ++s) e.processBlock(beat(s));
            commit(e, singleHitAt(8, 12), 8);              // 12 steps starting at step 8, hit on its step 8
            for (int s = 10; s < 16; ++s) e.processBlock(beat(s));
            // The wrap realigns the pattern to step 0 (a rest); the hit on its step 8 is not crossed.
            expect(! fired(e.processBlock(beat(16))), "the wrap block must not replay the skipped steps");
        }

        beginTest("The first call at step 0 reports a pattern pass start");
        {
            SequencerEngine e;
            e.addRhythm(restOnZero(3, 8));
            expect((e.processBlock(beat(0)).rhythmLoopWrapMask & 1) != 0, "step 0 on a cold start is a loop boundary");
        }

        beginTest("A knob change in the boundary step, then a commit in the same step, does not double hit");
        {
            SequencerEngine e;
            e.addRhythm(rhythmOf(8));                      // old: every step hits
            for (int s = 0; s <= 8; ++s) e.processBlock(beat(s));   // the old pattern sounded step 8
            e.getRhythm(0) = restOnZero(3, 8);             // the Euclid knob: step 0 is now a rest
            e.updatePattern(0);
            expect(! fired(e.processBlock(beat(8) + 0.05)), "the knob change absorbs the step, nothing plays");
            commit(e, rhythmOf(4, 8), 8);                  // new: a hit on step 0
            expect(! fired(e.processBlock(beat(8) + 0.10)), "the step already sounded once: no second hit");
            expect(fired(e.processBlock(beat(9))) || fired(e.processBlock(beat(10))), "the new pattern plays on");
        }

        beginTest("An absorbed, never-walked step still fires exactly once when a commit follows");
        {
            SequencerEngine e;
            e.addRhythm(restOnZero(3, 8));                 // old: step 8 (its step 0) is a rest
            for (int s = 0; s <= 8; ++s) e.processBlock(beat(s));
            e.getRhythm(0) = rhythmOf(8);                  // the knob makes step 0 a hit, absorbed unwalked
            e.updatePattern(0);
            int n = fired(e.processBlock(beat(8) + 0.05)) ? 1 : 0;
            commit(e, rhythmOf(4, 8), 8);                  // new: a hit on step 0
            for (double off = 0.10; off < 0.24; off += 0.05)
                n += fired(e.processBlock(beat(8) + off)) ? 1 : 0;
            expectEquals(n, 1, "nothing sounded in step 8 until the commit: the new downbeat plays once");
        }

        beginTest("A swap start with legato: the new pattern's first hit is not tied to the old pattern");
        {
            SequencerEngine e;
            e.addRhythm(rhythmOf(8));                      // old: every step hits
            for (int s = 0; s <= 8; ++s) e.processBlock(beat(s));
            commit(e, rhythmOf(8), 8);                     // new: hits on 0 and 1, landing mid step 8
            expect(! fired(e.processBlock(beat(8) + 0.05)), "the boundary step is absorbed");
            const auto r = e.processBlock(beat(9));
            expect(fired(r), "step 1 of the new pattern fires");
            expect((r.tiedMask & 1) == 0, "a fresh voice's first hit must not be tied");
        }

        beginTest("A master wrap re-seeds the tie flag from the step the position lands on");
        {
            // 7 steps in a 16-step loop: step 15 is pattern step 1, but the wrap realigns to step 0
            // whose predecessor is pattern step 6. Pick a pattern whose steps 0 and 1 hit and 6 rests.
            Rhythm pat;
            bool found = false;
            for (int hits = 2; hits < 7 && ! found; ++hits)
                for (int rot = 0; rot < 7 && ! found; ++rot)
                {
                    Rhythm c = rhythmOf(hits, 7);
                    c.genA.rotate = rot;
                    const auto p = c.getCombinedPattern();
                    if (p[0] && p[1] && ! p[6]) { pat = c; found = true; }
                }
            expect(found, "test pattern exists");
            SequencerEngine e;
            e.addRhythm(pat);
            e.setMasterLoopSteps(16);
            for (int s = 0; s < 16; ++s) e.processBlock(beat(s));
            const auto r = e.processBlock(beat(16));       // the wrap: pattern step 0 is a hit after a rest
            expect(fired(r), "pattern step 0 fires at the wrap");
            expect((r.tiedMask & 1) == 0, "step 6 was a rest, so the hit after the wrap is not tied");
        }

        beginTest("A loop-length change and a boundary-step commit in one block do not double hit");
        {
            SequencerEngine e;
            e.addRhythm(rhythmOf(8));
            for (int s = 0; s <= 8; ++s) e.processBlock(beat(s));   // the old pattern sounded step 8
            e.setMasterLoopSteps(16);
            commit(e, rhythmOf(8), 8);
            expect(! fired(e.processBlock(beat(8) + 0.05)), "the step already sounded, the new one starts on the next");
            expect(fired(e.processBlock(beat(9))), "and it keeps going");
        }

        beginTest("A pattern whose length does not divide the loop neither collapses a block nor flags a false pass start");
        {
            SequencerEngine e;
            e.addRhythm(singleHitAt(3, 7));
            e.setMasterLoopSteps(16);
            int fires = 0, passStarts = 0;
            for (int s = 0; s < 48; ++s)
            {
                const auto r = e.processBlock(beat(s));
                if (fired(r)) ++fires;
                if (r.rhythmLoopWrapMask & 1) ++passStarts;
            }
            expectEquals(fires, 6, "steps 3 and 10 of every loop, nothing replayed at the wrap");
            expectEquals(passStarts, 9, "steps 0, 7 and 14 of every loop");
        }

        beginTest("A swap staged and the transport started at step 0: one trigger, one commit");
        {
            SequencerEngine e;
            e.addRhythm(rhythmOf(4, 8));                   // old pattern, loaded while stopped
            mu_hotswap::Stager<int, int, 1> stager;
            stager.stage(0, 7);
            e.resetWrapDetector();                         // play pressed
            int triggers = 0, commits = 0;
            const auto first = e.processBlock(beat(0));    // the cold start is a pass start: flags the swap
            if (fired(first)) ++triggers;
            const auto token = stager.pendingToken(0);
            stager.flagIfReady(0, token, (first.rhythmLoopWrapMask & 1) != 0);
            // The commit lands a poll later, in the same step.
            if (stager.consume(0, [&](int&) { commit(e, rhythmOf(4, 8), 0); ++commits; })) {}
            for (double off = 0.05; off < 0.24; off += 0.05)
                if (fired(e.processBlock(beat(0) + off))) ++triggers;
            for (int s = 1; s < 8; ++s)
            {
                const auto r = e.processBlock(beat(s));
                stager.flagIfReady(0, stager.pendingToken(0), (r.rhythmLoopWrapMask & 1) != 0);
                if (stager.consume(0, [&](int&) { ++commits; })) {}
            }
            expectEquals(triggers, 1, "step 0 sounds once");
            expectEquals(commits, 1, "the staged swap commits once");
        }

        beginTest("A length change seen by the modulation recompute keeps the pattern's phase");
        {
            // The Steps knob moved the live rhythm from 5 to 7 steps before the snapshot landed; the
            // modulation recompute is the first to build the longer pattern.
            SequencerEngine e;
            e.addRhythm(rhythmOf(1, 5));                   // one hit on step 0
            for (int s = 0; s <= 7; ++s) e.processBlock(beat(s));   // old position 2
            auto& live = e.getRhythm(0);
            live.genA.steps = 7;
            live.genB.steps = 7;
            EuclidOverrides ov;
            ov.a.hits = 1; ov.b.hits = 0; ov.c.hits = 0;
            expect(e.tryUpdatePatternFromModulation(0, ov));
            expectEquals(e.getPatternLength(0), 7, "the recompute built the longer pattern");
            // The position is re-derived to 0, so step 8 (position 1, a rest) is one step on. Left at the
            // old 2, the walk would cross the hit on position 0 and fire.
            const auto r = e.processBlock(beat(8));
            expect(! fired(r), "no steps replayed after a length change");
            expect((r.rhythmLoopWrapMask & 1) == 0, "and no false pass start");
        }

        beginTest("Removing a slot while a reorder is pending renumbers the partner it adopts from");
        {
            SequencerEngine e;
            e.addRhythm(rhythmOf(0, 8));                   // slot 0: silent, removed below
            e.addRhythm(rhythmOf(0, 8));                   // slot 1
            e.addRhythm(singleHitAt(0, 8));                // slot 2: one hit on its step 0
            for (int s = 0; s < 10; ++s) e.processBlock(beat(s));
            e.requestSwapStart(2, 3);                      // its step 0 now falls on effective steps 3, 11, ...
            e.processBlock(beat(10));
            e.swapRhythmSlots(1, 2);                       // the hit pattern moves to slot 1, adopting slot 2's phase
            e.removeRhythm(0);                             // ... and shifts to slot 0 before the audio thread sees it
            e.processBlock(beat(10) + 0.05);
            expect(fired(e.processBlock(beat(11))), "the moved pattern keeps its phase: its step 0 is effective step 11");
        }

        beginTest("A backward transport jump mid-loop is not a master wrap; the real wrap still is");
        for (bool guarded : { false, true })
        {
            SequencerEngine e;
            e.addRhythm(rhythmOf(4, 16));
            e.setMasterLoopSteps(16);
            double prev = 0.0;
            int wraps = 0;
            const auto play = [&](double b)
            {
                if (guarded && SequencerEngine::isBackwardBeatJump(prev, b, e.getMasterLoopSteps())) e.resetWrapDetector();
                prev = b;
                if (e.processBlock(b).masterLoopWrapped) ++wraps;
            };
            for (int s = 0; s < 12; ++s) play(beat(s));
            play(beat(3));                                 // the host loop jumps back mid-loop
            const int afterJump = wraps;
            for (int s = 4; s < 16; ++s) play(beat(s));
            const int beforeRealWrap = wraps;
            play(beat(16));                                // the real wrap
            if (guarded)
            {
                expectEquals(afterJump, 0, "the jump must not report a wrap");
                expectEquals(beforeRealWrap, 0, "no wrap before the real boundary");
                expectEquals(wraps, 1, "the real wrap is still reported once");
            }
            else
                expectEquals(afterJump, 1, "without the reset the jump is mistaken for a wrap");
        }

        beginTest("A host loop that lands on a master-loop boundary is still a wrap");
        for (int from : { 12, 28 })
        {
            // 12 -> 0 (the loop start) and 28 -> 16 (a later pass): both land on a boundary.
            SequencerEngine e;
            e.addRhythm(rhythmOf(4, 16));
            e.setMasterLoopSteps(16);
            const int landing = from == 12 ? 0 : 16;
            double prev = 0.0;
            int wraps = 0;
            const auto play = [&](double b)
            {
                if (SequencerEngine::isBackwardBeatJump(prev, b, e.getMasterLoopSteps())) e.resetWrapDetector();
                prev = b;
                if (e.processBlock(b).masterLoopWrapped) ++wraps;
            };
            for (int s = from == 12 ? 0 : 16; s <= from; ++s) play(beat(s));
            expectEquals(wraps, 0, "no wrap on the way up");
            play(beat(landing));
            expectEquals(wraps, 1, "the jump to the boundary is reported as one wrap");
        }

        beginTest("Backward moves are compared by step");
        {
            expect(! SequencerEngine::isBackwardBeatJump(4.1, 4.05), "a dip inside one step");
            expect(  SequencerEngine::isBackwardBeatJump(4.05, 3.95), "a sub-step dip across a step line");
            expect(  SequencerEngine::isBackwardBeatJump(4.0, 1.0), "a locate back");
            expect(! SequencerEngine::isBackwardBeatJump(4.0, 4.5), "forward");
            expect(! SequencerEngine::isBackwardBeatJump(3.0, 0.0, 16), "a jump onto a loop boundary is a real wrap");
            expect(  SequencerEngine::isBackwardBeatJump(4.0, 1.0, 16), "a mid-loop landing still resets");
            expect(  SequencerEngine::isBackwardBeatJump(4.01, 3.99, 4), "a sub-step dip onto a boundary still resets");
        }

        beginTest("A sub-step dip across a step line is not a master wrap");
        {
            SequencerEngine e;
            e.addRhythm(rhythmOf(4, 16));
            e.setMasterLoopSteps(16);
            double prev = 0.0;
            int wraps = 0;
            const auto play = [&](double b)
            {
                if (SequencerEngine::isBackwardBeatJump(prev, b, e.getMasterLoopSteps())) e.resetWrapDetector();
                prev = b;
                if (e.processBlock(b).masterLoopWrapped) ++wraps;
            };
            for (int s = 0; s < 6; ++s) play(beat(s));
            play(beat(6) + 0.01);
            play(beat(6) - 0.04);                          // a nudge dips back across the step line
            play(beat(7));
            expectEquals(wraps, 0, "the dip must not report a wrap");
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
