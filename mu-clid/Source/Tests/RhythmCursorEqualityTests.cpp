// The per-layer step cursor against mu_core::RhythmSequencer (E5.5a gate): the same beats, patterns,
// swap starts, modulated patterns and loop / restart requests go to both, and every block's output must be
// equal: fired / accent / tied / loop-wrap bits, the master-loop wrap, the walked step and accent step, the
// pattern length, the swap boundary and the step-counter run id. Bit for bit, no tolerance.

#include <juce_core/juce_core.h>
#include "Sequencer/GeneratorAliases.h"
#include "Sequencer/RhythmCursor.h"
#include "SequencerTestHarness.h"

#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>

using namespace mu_clid;

namespace
{
    constexpr int kCap = 64;

    // The cursor side: one host clock and one cursor per rhythm, with rhythms of its own.
    struct CursorRig
    {
        RhythmStepClock clock;
        std::array<RhythmCursor, RhythmSequencer::MaxRhythms> cursors;
        std::array<Rhythm, RhythmSequencer::MaxRhythms>       rhythms;
        int n = 0;

        void addRhythm(const Rhythm& r)
        {
            rhythms[(size_t) n] = r;
            cursors[(size_t) n].updatePattern(rhythms[(size_t) n], kCap);
            ++n;
        }
    };

    // Two rhythms driven in lock step, plus the comparison.
    struct Pair
    {
        TestSequencer seq;
        CursorRig     rig;
        int firedBlocks = 0, wrapBlocks = 0;   // so a run that compares nothing cannot pass

        Pair() { seq.setStepCap(kCap); }

        void addRhythm(const Rhythm& r) { seq.addRhythm(r); rig.addRhythm(r); }

        void setLoop(int steps) { seq.setMasterLoopSteps(steps); rig.clock.setMasterLoopSteps(steps); }
        void restart()          { seq.resetWrapDetector();       rig.clock.resetWrapDetector(); }

        // Changes rhythm r's data on both sides and rebuilds its pattern (absorbed at the next block).
        void edit(int r, const std::function<void(Rhythm&)>& change)
        {
            change(seq.getRhythm(r));
            seq.updatePattern(r);
            change(rig.rhythms[(size_t) r]);
            rig.cursors[(size_t) r].updatePattern(rig.rhythms[(size_t) r], kCap);
        }

        // A hot-swap commit: the new rhythm starts at the boundary each side recorded (or at `origin`).
        void swapStart(int r, const Rhythm& incoming, bool atRecordedBoundary, int origin = 0)
        {
            const int      seqOrigin = atRecordedBoundary ? seq.getBoundaryEff(r) : origin;
            const uint32_t seqEpoch  = atRecordedBoundary ? seq.getFrameEpoch() : RhythmSequencer::kAnyFrameEpoch;
            const int      rigOrigin = atRecordedBoundary ? rig.cursors[(size_t) r].boundaryEffective() : origin;
            const uint32_t rigEpoch  = atRecordedBoundary ? rig.clock.currentEpoch() : RhythmCursor::kAnyFrameEpoch;
            seq.getRhythm(r) = incoming;
            seq.requestSwapStart(r, seqOrigin, seqEpoch);
            rig.rhythms[(size_t) r] = incoming;
            rig.cursors[(size_t) r].requestSwapStart(rig.rhythms[(size_t) r], kCap, rigOrigin, rigEpoch);
        }

        // A modulated pattern (as the modulation pass applies it), both sides.
        bool modulate(int r, const EuclidOverrides& ov)
        {
            const bool a = seq.tryUpdatePatternFromModulation(r, ov);
            const bool b = rig.cursors[(size_t) r].tryApplyModulatedPattern(rig.rhythms[(size_t) r], ov, rig.clock.lastEffective());
            return a && b;
        }

        // One block at `beat` on both sides; the first difference is described, empty when equal.
        juce::String block(double beat)
        {
            const auto res = seq.processBlock(beat);

            const auto f = rig.clock.advance(beat);
            int fired = 0, accent = 0, tied = 0, wrap = 0;
            for (int r = 0; r < rig.n; ++r)
            {
                const auto h = rig.cursors[(size_t) r].advance(f);
                if (h.fired)       fired  |= 1 << r;
                if (h.accented)    accent |= 1 << r;
                if (h.tied)        tied   |= 1 << r;
                if (h.loopWrapped) wrap   |= 1 << r;
            }
            rig.clock.endBlock(f);
            if (res.firedMask != 0)          ++firedBlocks;
            if (res.rhythmLoopWrapMask != 0) ++wrapBlocks;

            auto diff = [&](const char* what, int a, int b) -> juce::String
            {
                return a == b ? juce::String() : juce::String(what) + " sequencer " + juce::String(a) + " cursor " + juce::String(b);
            };
            juce::String d;
            auto check = [&](juce::String s) { if (d.isEmpty() && s.isNotEmpty()) d = s; };
            check(diff("fired",  res.firedMask,  fired));
            check(diff("accent", res.accentMask, accent));
            check(diff("tied",   res.tiedMask,   tied));
            check(diff("loopWrap", res.rhythmLoopWrapMask, wrap));
            check(diff("masterWrap", (int) res.masterLoopWrapped, (int) f.masterLoopWrapped));
            check(diff("epoch", (int) seq.getFrameEpoch(), (int) rig.clock.currentEpoch()));
            check(diff("masterStep", seq.getMasterLoopCurrentStep(), rig.clock.getMasterLoopCurrentStep()));
            for (int r = 0; r < rig.n; ++r)
            {
                const auto& c = rig.cursors[(size_t) r];
                const juce::String at = " (rhythm " + juce::String(r) + ")";
                check(diff(("step" + at).toRawUTF8(),       seq.getLastStepIndex(r),       c.lastStep()));
                check(diff(("accentStep" + at).toRawUTF8(), seq.getLastAccentStepIndex(r), c.lastAccentStep()));
                check(diff(("length" + at).toRawUTF8(),     seq.getPatternLength(r),       c.patternLength()));
                check(diff(("boundary" + at).toRawUTF8(),   seq.getBoundaryEff(r),         c.boundaryEffective()));
            }
            return d;
        }
    };

    Rhythm makeRhythm(int stepsA, int hitsA, int rotA, int stepsB, int hitsB, int stepsC, int hitsC,
                      Logic logic = Logic::OR, bool legato = false)
    {
        Rhythm r;
        r.genA.steps = stepsA; r.genA.hits = hitsA; r.genA.rotate = rotA;
        r.genB.steps = stepsB; r.genB.hits = hitsB;
        r.genC.steps = stepsC; r.genC.hits = hitsC;
        r.logic = logic;
        r.patternLegato = legato;
        return r;
    }

    // A small deterministic generator for block lengths and jitter (no std::rand: the same sequence everywhere).
    struct Lcg
    {
        uint32_t s;
        uint32_t next() noexcept { s = s * 1664525u + 1013904223u; return s >> 8; }
        double   unit() noexcept { return (double) next() / (double) (1u << 24); }
    };
}

class RhythmCursorEqualityTests : public juce::UnitTest
{
public:
    RhythmCursorEqualityTests() : juce::UnitTest("Rhythm cursor equals RhythmSequencer", "RhythmSequencer") {}

    // Runs `blocks` blocks from `startBeat`, `beatsPerBlock(i)` apart, calling `between(i)` before block i.
    void run(Pair& p, double startBeat, int blocks, const std::function<double(int)>& beatsPerBlock,
             const std::function<void(int, double&)>& between = {})
    {
        double beat = startBeat;
        for (int i = 0; i < blocks; ++i)
        {
            if (between) between(i, beat);
            const auto d = p.block(beat);
            if (d.isNotEmpty())
            {
                expect(false, "block " + juce::String(i) + " at beat " + juce::String(beat, 6) + ": " + d);
                return;
            }
            beat += beatsPerBlock(i);
        }
        expect(p.firedBlocks > blocks / 20 && p.wrapBlocks > 0,
               "the run fired (" + juce::String(p.firedBlocks) + " blocks) and wrapped (" + juce::String(p.wrapBlocks) + ")");
    }

    static void addSet(Pair& p)
    {
        p.addRhythm(makeRhythm(16, 5, 0, 12, 3, 8, 3));
        p.addRhythm(makeRhythm(7, 4, 2, 5, 2, 3, 1, Logic::XOR, true));
        p.addRhythm(makeRhythm(13, 13, 0, 4, 1, 16, 4, Logic::AND));
        p.addRhythm(makeRhythm(1, 1, 0, 1, 0, 2, 1));   // a one-step pattern (tiled to two)
    }

    void runTest() override
    {
        // 512 frames at 48 kHz and 120 bpm, and a few other block lengths in beats.
        const double b512 = 512.0 / 48000.0 * 2.0;

        beginTest("free-running, fixed blocks");
        { Pair p; addSet(p); run(p, 0.0, 4000, [=](int) { return b512; }); }

        beginTest("blocks longer than a step and ragged blocks");
        {
            Pair p; addSet(p);
            const double sizes[] = { 0.001, 0.02, 0.3, 0.7, 1.9, 0.05, 0.25, 0.2499999999 };
            run(p, 0.0, 3000, [&](int i) { return sizes[(size_t) i % std::size(sizes)]; });
        }

        beginTest("master loop wraps (lengths that do and do not divide the loop)");
        {
            for (int loop : { 16, 32, 12, 7 })
            {
                Pair p; addSet(p); p.setLoop(loop);
                run(p, 0.0, 5000, [=](int) { return b512; });
            }
        }

        beginTest("tempo changes and a tempo ramp");
        {
            Pair p; addSet(p); p.setLoop(32);
            run(p, 0.5, 6000, [=](int i) { return b512 * (0.25 + 3.75 * (0.5 + 0.5 * std::sin(i * 0.003))); });
        }

        beginTest("loop restarts, transport restarts and backward jumps");
        {
            Pair p; addSet(p); p.setLoop(16);
            run(p, 0.0, 6000, [=](int) { return b512; }, [&](int i, double& beat)
            {
                if (i % 700 == 350) { p.restart(); beat = 0.0; }               // play pressed again at the top
                if (i % 900 == 450) beat = std::max(0.0, beat - 3.3);          // a host loop jump back
                if (i % 1100 == 10) beat -= 0.1;                               // a sub-step dip
                if (i % 1300 == 600) p.setLoop(p.seq.getMasterLoopSteps() == 16 ? 32 : 16);
                if (i % 1700 == 900) beat = std::numeric_limits<double>::quiet_NaN();   // a broken host position
            });
        }

        beginTest("pattern edits mid-play (absorbed) and swap starts at the recorded boundary and at fixed origins");
        {
            Pair p; addSet(p); p.setLoop(32);
            Lcg rng { 12345u };
            run(p, 0.0, 8000, [=](int) { return b512; }, [&](int i, double&)
            {
                if (i % 97 == 13)
                {
                    const int      r    = (int) (rng.next() % 4);
                    const unsigned roll = rng.next();   // drawn once: the edit runs on both sides
                    p.edit(r, [roll](Rhythm& rh) { rh.genA.hits = (int) (roll % (unsigned) std::max(1, rh.genA.steps + 1)); });
                }
                if (i % 211 == 40)
                {
                    const int r = (int) (rng.next() % 4);
                    p.swapStart(r, makeRhythm(9 + (int) (rng.next() % 8), 3, 1, 6, 2, 4, 1), true);
                }
                if (i % 389 == 77)
                {
                    const int r = (int) (rng.next() % 4);
                    p.swapStart(r, makeRhythm(10, 7, 0, 3, 1, 5, 2, Logic::OR, true), false, (int) (rng.next() % 40));
                }
                if (i % 1500 == 1000) p.restart();   // a swap waiting across a restart starts from 0
            });
        }

        beginTest("stale epochs: swaps recorded just before a restart and just before a master wrap");
        {
            Pair p; addSet(p); p.setLoop(32);
            // 32 steps = 8 beats; with b512 (about 0.0213 beats) a wrap falls about every 375 blocks.
            run(p, 0.0, 4000, [=](int) { return b512; }, [&](int i, double& beat)
            {
                if (i % 500 == 250) { p.swapStart(i % 4, makeRhythm(11, 4, 1, 6, 2, 4, 1), true); p.restart(); beat = 0.25; }
                const double toWrap = 8.0 - std::fmod(beat, 8.0);
                if (toWrap > 0.0 && toWrap <= b512 && i > 5)
                    p.swapStart((i / 7) % 4, makeRhythm(9, 5, 2, 7, 3, 5, 2), true);
            });
        }

        beginTest("modulation mixed with edits and swap starts; pre-roll beats; free-running to a loop mid-play");
        {
            Pair p; addSet(p);   // free-running first
            Lcg rng { 4242u };
            run(p, -1.5, 7000, [=](int) { return b512; }, [&](int i, double&)
            {
                if (i == 3000) p.setLoop(16);
                if (i == 5000) p.setLoop(0);
                if (i % 23 == 4)
                {
                    const int r = (int) (rng.next() % 3);
                    const Rhythm& base = p.rig.rhythms[(size_t) r];
                    EuclidOverrides ov;
                    auto set = [&](EuclidGenOverrides& g, const HitGenerator& src)
                    {
                        g.hits = (int) (rng.next() % (unsigned) std::max(1, src.steps + 1));
                        g.rotate = (int) (rng.next() % (unsigned) std::max(1, src.steps));
                        g.prePad = (int) (rng.next() % 2);
                        g.postPad = (int) (rng.next() % 2);
                        g.insertStart = 0;
                        g.insertLength = 0;
                    };
                    set(ov.a, base.genA); set(ov.b, base.genB); set(ov.c, base.genC);
                    expect(p.modulate(r, ov));
                }
                if (i % 89 == 30)
                {
                    const int      r    = (int) (rng.next() % 4);
                    const unsigned roll = rng.next();
                    p.edit(r, [roll](Rhythm& rh) { rh.genB.hits = (int) (roll % (unsigned) std::max(1, rh.genB.steps + 1)); });
                }
                if (i % 157 == 60)
                    p.swapStart((int) (rng.next() % 4), makeRhythm(8 + (int) (rng.next() % 9), 3, 0, 5, 2, 6, 2), (rng.next() & 1) != 0,
                                (int) (rng.next() % 20));
            });
        }

        beginTest("modulated patterns change length and phase mid-play");
        {
            Pair p; addSet(p); p.setLoop(16);
            Lcg rng { 777u };
            run(p, 0.0, 6000, [=](int) { return b512; }, [&](int i, double&)
            {
                if (i % 31 == 5)
                {
                    const int r = (int) (rng.next() % 3);
                    const Rhythm& base = p.rig.rhythms[(size_t) r];
                    EuclidOverrides ov;
                    auto set = [&](EuclidGenOverrides& g, const HitGenerator& src)
                    {
                        g.hits = (int) (rng.next() % (unsigned) std::max(1, src.steps + 1));
                        g.rotate = (int) (rng.next() % (unsigned) std::max(1, src.steps));
                        g.prePad = (int) (rng.next() % 3);
                        g.postPad = (int) (rng.next() % 3);
                        g.insertStart = 0;
                        g.insertLength = 0;
                    };
                    set(ov.a, base.genA); set(ov.b, base.genB); set(ov.c, base.genC);
                    expect(p.modulate(r, ov), "no contention in a single-threaded test");
                }
            });
        }
    }
};

static RhythmCursorEqualityTests rhythmCursorEqualityTests;
