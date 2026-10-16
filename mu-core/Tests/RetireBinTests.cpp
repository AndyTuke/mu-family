// The shared RetireBin (C2) and frozen retire snapshots (C3): the state machine checked exhaustively by a
// deterministic two-actor scheduler, depth and eviction, fades and lifetimes, the activation serial, the
// in-place rules R1, the message-side serialisation R2, the count policies R3, the failed block R4, the tail's
// MIDI sink R7, and the isolation twin test. docs/mu-tronic/design.md 2.1.3, 2.1.4; tests.md section 6.6.

#include "LayerContractFixtures.h"

#include <array>
#include <thread>
#include <vector>

using namespace mu_contract_test;
using Bin = mu_core::RetireBin;

namespace
{
    // A tail that lives in a fixed arena with a do-nothing operator delete, so a render after its destruction
    // reads a dead magic instead of freed memory: the scheduler can detect a use-after-free without crashing.
    struct ProbeTail : mu_core::RetireTail
    {
        static void* operator new(std::size_t n)
        {
            static std::array<std::array<std::max_align_t, 32>, 4096> arena;
            static int next = 0;
            jassert(n <= sizeof (arena[0]));
            juce::ignoreUnused(n);
            return &arena[(size_t) (next++ % (int) arena.size())];
        }
        static void operator delete(void*) noexcept {}

        explicit ProbeTail(mu_core::RetirePolicy p = { mu_core::RetirePolicy::Kind::Natural, 25.0f, 2.0f }) : pol(p) {}
        ~ProbeTail() override
        {
            magic = kDeadMagic;
            ++destroyed();
            if (! mu_core::RetireBin::isFreeingOnThisThread()) ++freedOutsideBin();
            if (mu_core::RetireBin::isMutexHeldOnThisThread()) ++freedUnderMutex();
            if (blockOpen != nullptr && *blockOpen && touchedThisBlock) ++freedUnderReader();
        }

        mu_core::RetirePolicy retirePolicy() const override { return pol; }
        void onRetireStart(const mu_core::RenderContext& ctx) override
        {
            ++starts;
            pathOk = pathOk && inRetirePath();
            if (owesNoteOff) ctx.emitMidi(0, 0x80, 60, 0);
        }
        bool renderRetiring(const mu_core::RenderContext& ctx, juce::AudioBuffer<float>& out, int n, mu_core::RetireState& rs) override
        {
            if (magic != kAliveMagic) ++useAfterFree();
            touchedThisBlock = true;
            ++renders;
            framesRendered += n;
            pathOk = pathOk && inRetirePath();
            ctxOk = ctxOk && ctx.retiring == 1 && ctx.midi.count == 0 && ctx.peers == nullptr && ctx.peer(0) == nullptr;
            for (int c = 0; c < out.getNumChannels(); ++c)
            {
                float* d = out.getWritePointer(c);
                for (int i = 0; i < n; ++i) d[i] = rs.exclusive ? value : d[i] + value;
            }
            if (pol.kind == mu_core::RetirePolicy::Kind::Fade) mu_core::applyLinearFade(out, n, rs);
            return silentAfter < 0 || renders < silentAfter;
        }

        static int& destroyed()        { static int n = 0; return n; }
        static int& useAfterFree()     { static int n = 0; return n; }
        static int& freedOutsideBin()  { static int n = 0; return n; }
        static int& freedUnderReader() { static int n = 0; return n; }
        static int& freedUnderMutex()  { static int n = 0; return n; }

        uint32_t magic = kAliveMagic;
        mu_core::RetirePolicy pol;
        float value = 1.0f;
        int   silentAfter = -1;   // renderRetiring returns false on this call (-1 = never)
        bool  owesNoteOff = false;
        int   starts = 0, renders = 0;
        int64_t framesRendered = 0;
        bool  pathOk = true, ctxOk = true;
        bool  touchedThisBlock = false;
        const bool* blockOpen = nullptr;
    };

    struct BinRig
    {
        explicit BinRig(double sr = 48000.0, int bs = 512) : bin(epoch) { bin.setPrepared(sr, bs); out.setSize(2, bs); }
        // One audio block: enter, render the slot's tails into a cleared buffer, leave.
        bool block(int slot, int n, uint32_t serial, bool slotHasLive = false)
        {
            mu_core::ScopedAudioEpoch b(epoch);
            juce::AudioBuffer<float> view(out.getArrayOfWritePointers(), 2, n);
            view.clear();
            auto ctx = mu_core::makeRenderContext();
            ctx.sampleRate = 48000.0;
            ctx.blockSize = n;
            ctx.preparedBlockSize = out.getNumSamples();
            ctx.slot = slot;
            return bin.render(slot, ctx, view, n, serial, slotHasLive);
        }
        mu_core::AudioEpoch epoch;
        Bin bin;
        juce::AudioBuffer<float> out;
    };

    bool legalTransition(int from, int to)
    {
        if (from == to) return true;
        return (from == Bin::Empty && to == Bin::Parked) || (from == Bin::Parked && to == Bin::Draining)
            || (from == Bin::Parked && to == Bin::Done) || (from == Bin::Draining && to == Bin::Done)
            || (from == Bin::Done && to == Bin::Empty);
    }
}

struct RetireBinStateMachineTests : juce::UnitTest
{
    RetireBinStateMachineTests() : juce::UnitTest("RetireBin state machine (C2, exhaustive interleavings)", "mu-core") {}

    void runTest() override
    {
        beginTest("Every interleaving of park / cut / service with block start / render / end: legal transitions, "
                  "no render after free, no free under a reader, freed exactly once");
        int cases = 0, illegal = 0;
        const int destroyedBefore = ProbeTail::destroyed(), uafBefore = ProbeTail::useAfterFree();
        const int underBefore = ProbeTail::freedUnderReader(), outsideBefore = ProbeTail::freedOutsideBin();
        for (int variant = 0; variant < 2; ++variant)          // 0: only a cut ends it; 1: it goes silent at its first render
            for (int mask = 0; mask < (1 << 10); ++mask)
            {
                int bits = 0;
                for (int b = 0; b < 10; ++b) bits += (mask >> b) & 1;
                if (bits != 4) continue;                       // 4 message steps among 10
                ++cases;
                BinRig rig;
                bool blockOpen = false;
                auto tail = std::unique_ptr<ProbeTail>(new ProbeTail());
                tail->blockOpen = &blockOpen;
                if (variant == 1) tail->silentAfter = 1;
                auto* t = tail.get();
                int msgStep = 0, audStep = 0;
                int prev = Bin::Empty;
                for (int step = 0; step < 10; ++step)
                {
                    if ((mask >> step) & 1)
                    {
                        switch (msgStep++)
                        {
                            case 0:  rig.bin.park(0, std::move(tail), 0, false); break;
                            case 1:  rig.bin.cutSlot(0); break;
                            default: rig.bin.service(); break;
                        }
                    }
                    else
                    {
                        switch (audStep++ % 3)
                        {
                            case 0: rig.epoch.enter(); blockOpen = true; if (t->magic == kAliveMagic) t->touchedThisBlock = false; break;
                            case 1:
                            {
                                juce::AudioBuffer<float> view(rig.out.getArrayOfWritePointers(), 2, 64);
                                view.clear();
                                auto ctx = mu_core::makeRenderContext();
                                ctx.blockSize = 64;
                                rig.bin.render(0, ctx, view, 64, 0, false);
                                break;
                            }
                            default: rig.epoch.leave(); blockOpen = false; break;
                        }
                    }
                    const int now = rig.bin.stateOf(0, 0);
                    if (! legalTransition(prev, now)) ++illegal;
                    prev = now;
                }
                rig.bin.cutAll();
                rig.bin.service();
                expect(rig.bin.stateOf(0, 0) == Bin::Empty);
                if (tail != nullptr) tail.reset();   // never parked (the park step came... it always runs; defensive)
            }
        expectEquals(cases, 420);
        expectEquals(illegal, 0, "every observed transition is in the table");
        expectEquals(ProbeTail::destroyed() - destroyedBefore, cases, "each tail is freed exactly once");
        expectEquals(ProbeTail::useAfterFree() - uafBefore, 0, "no render after a free");
        expectEquals(ProbeTail::freedUnderReader() - underBefore, 0, "never freed inside a block that rendered it");
        expectEquals(ProbeTail::freedOutsideBin() - outsideBefore, 0, "only the bin's free path destroys a tail");

        beginTest("Spot transitions: Parked to Draining on the first reached render (onRetireStart once), Draining to Done, Done to Empty after grace");
        {
            BinRig rig;
            auto* t = new ProbeTail();
            t->silentAfter = 3;
            expect(rig.bin.park(0, std::unique_ptr<mu_core::RetireTail>(t), 5, false) == Bin::ParkResult::Parked);
            expect(rig.bin.stateOf(0, 0) == Bin::Parked);
            rig.block(0, 64, 4);   // serial 4 has not reached activation 5
            expect(rig.bin.stateOf(0, 0) == Bin::Parked && t->starts == 0);
            rig.block(0, 64, 5);
            expect(rig.bin.stateOf(0, 0) == Bin::Draining && t->starts == 1);
            rig.block(0, 64, 5);
            const bool wentDone = rig.block(0, 64, 5);
            expect(wentDone && rig.bin.stateOf(0, 0) == Bin::Done && t->starts == 1);
            expect(rig.bin.hasPendingWork());
            // Grace: a block still open at the first sight of Done defers the free.
            {
                rig.epoch.enter();
                expectEquals(rig.bin.service(), 0);
                expect(rig.bin.stateOf(0, 0) == Bin::Done);
                rig.epoch.leave();
            }
            expectEquals(rig.bin.service(), 1);
            expect(rig.bin.stateOf(0, 0) == Bin::Empty && ! rig.bin.hasPendingWork());
        }

        beginTest("Kind None is not parked: it waits out the grace in the reclaim list");
        {
            BinRig rig;
            const int before = ProbeTail::destroyed();
            rig.epoch.enter();
            expect(rig.bin.park(0, std::unique_ptr<mu_core::RetireTail>(new ProbeTail({})), 0, false) == Bin::ParkResult::NotParked);
            expectEquals(rig.bin.countIn(Bin::Parked), 0);
            expectEquals(rig.bin.service(), 0, "the block that may still read it is open");
            rig.epoch.leave();
            expectEquals(rig.bin.service(), 1);
            expectEquals(ProbeTail::destroyed() - before, 1);
        }

        beginTest("A tail context: retiring, no MIDI in, no peers, the bin's flag on during both tail calls");
        {
            BinRig rig;
            auto* t = new ProbeTail();
            rig.bin.park(0, std::unique_ptr<mu_core::RetireTail>(t), 0, false);
            rig.block(0, 64, 0);
            rig.block(0, 64, 0);
            expect(t->ctxOk && t->pathOk && ! t->inRetirePath());
        }
    }
};
static RetireBinStateMachineTests sRetireBinStateMachineTests;

struct RetireBinDepthTests : juce::UnitTest
{
    RetireBinDepthTests() : juce::UnitTest("RetireBin depth, eviction, fades and lifetimes (C2)", "mu-core") {}

    void runTest() override
    {
        beginTest("N-deep: four tails in one slot; a fifth evicts entry 0 at once under layersLock (freed later, off the lock)");
        {
            BinRig rig;
            std::array<ProbeTail*, 5> t {};
            for (int i = 0; i < 4; ++i)
            {
                t[(size_t) i] = new ProbeTail();
                expect(rig.bin.park(0, std::unique_ptr<mu_core::RetireTail>(t[(size_t) i]), 0, true) == Bin::ParkResult::Parked);
            }
            const int before = ProbeTail::destroyed();
            t[4] = new ProbeTail();
            expect(rig.bin.park(0, std::unique_ptr<mu_core::RetireTail>(t[4]), 0, true) == Bin::ParkResult::Parked);
            expect(rig.bin.tailOf(0, 0) == t[4], "entry 0 was force-cut and reused (mu-Clid's rule)");
            expectEquals(ProbeTail::destroyed() - before, 0, "nothing is freed while the caller holds layersLock");
            expectEquals(rig.bin.reclaimPending(), 1);
            expectEquals(rig.bin.service(), 1);
            expectEquals(ProbeTail::destroyed() - before, 1);
        }

        beginTest("Eviction outside layersLock waits one grace; on a timeout the INCOMING tail is dropped (N2)");
        {
            BinRig rig;
            rig.bin.setEvictTimeoutMs(20);
            for (int i = 0; i < 4; ++i) rig.bin.park(0, std::unique_ptr<mu_core::RetireTail>(new ProbeTail()), 0, false);
            {
                struct Inside { mu_core::AudioEpoch& e; juce::WaitableEvent in, out; std::thread t;
                                explicit Inside(mu_core::AudioEpoch& ep) : e(ep), t([this] { e.enter(); in.signal(); out.wait(); e.leave(); }) { in.wait(); }
                                ~Inside() { out.signal(); t.join(); } } inside(rig.epoch);
                const int before = ProbeTail::destroyed();
                expect(rig.bin.park(0, std::unique_ptr<mu_core::RetireTail>(new ProbeTail()), 0, false) == Bin::ParkResult::Dropped);
                expectEquals(ProbeTail::destroyed() - before, 1, "the incoming tail never sounded: freed at once, after binMutex");
                expect(rig.bin.stateOf(0, 0) == Bin::Done, "the evicted entry stays Done for service");
            }
            expect(rig.bin.service() >= 1, "the evicted entry is freed once the block ended");
            expect(rig.bin.park(0, std::unique_ptr<mu_core::RetireTail>(new ProbeTail()), 0, false) == Bin::ParkResult::Parked);
        }

        beginTest("The global budget evicts the instance's oldest tail");
        {
            BinRig rig;
            rig.bin.setGlobalBudget(2);
            auto* a = new ProbeTail(); auto* b = new ProbeTail(); auto* c = new ProbeTail();
            rig.bin.park(0, std::unique_ptr<mu_core::RetireTail>(a), 0, false);
            rig.bin.park(3, std::unique_ptr<mu_core::RetireTail>(b), 0, false);
            rig.bin.park(5, std::unique_ptr<mu_core::RetireTail>(c), 0, false);
            expect(rig.bin.stateOf(0, 0) != Bin::Parked, "the oldest (slot 0) went");
            expect(rig.bin.tailOf(3, 0) == b && rig.bin.tailOf(5, 0) == c);
        }

        beginTest("Two retargets inside 25 ms both sound to their end");
        {
            BinRig rig;
            auto* a = new ProbeTail({ mu_core::RetirePolicy::Kind::Fade, 25.0f, 2.0f });
            auto* b = new ProbeTail({ mu_core::RetirePolicy::Kind::Fade, 25.0f, 2.0f });
            rig.bin.park(0, std::unique_ptr<mu_core::RetireTail>(a), 0, false);
            rig.block(0, 256, 0);
            rig.bin.park(0, std::unique_ptr<mu_core::RetireTail>(b), 0, false);
            int64_t aFrames = 0, bFrames = 0;
            for (int i = 0; i < 20; ++i) { rig.block(0, 256, 0); aFrames = a->framesRendered; bFrames = b->framesRendered; }
            expect(aFrames >= 1200 && bFrames >= 1200, "each fade ran its full 1200 frames");
            expectEquals(rig.bin.countIn(Bin::Done), 2);
        }

        beginTest("The fade: mu-Tant's ramp length at 44.1, 48, 96 and 192 kHz and its exact gain expression");
        for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            BinRig rig(sr, 512);
            auto* t = new ProbeTail({ mu_core::RetirePolicy::Kind::Fade, 25.0f, 2.0f });
            rig.bin.park(0, std::unique_ptr<mu_core::RetireTail>(t), 0, false);
            const int ramp = juce::jmax(1, (int) (sr * 0.025));   // mu-Tant's retireRampSamples()
            expectEquals(rig.bin.stateDataOf(0, 0).samplesLeft, ramp);
            expectEquals(rig.bin.stateDataOf(0, 0).gainStep, 1.0f / (float) ramp);
            rig.block(0, 512, 0);
            bool exact = true;
            for (int i = 0; i < 512; ++i)
                exact &= rig.out.getSample(0, i) == juce::jmax(0.0f, (float) (ramp - i) * (1.0f / (float) ramp));
            expect(exact, "gain at frame i = max(0, (left - i) * gainStep)");
            for (int k = 0; k < 40 && rig.bin.stateOf(0, 0) != Bin::Done; ++k) rig.block(0, 512, 0);
            expect(rig.bin.stateOf(0, 0) == Bin::Done, "Done once the ramp is spent");
            expect(t->framesRendered >= ramp && t->framesRendered < ramp + 512);
        }

        beginTest("The hard lifetime ends a tail that never reports silence, with a 10 ms linear declick");
        {
            BinRig rig(48000.0, 256);
            auto* t = new ProbeTail({ mu_core::RetirePolicy::Kind::Natural, 25.0f, 0.1f });   // 4800 frames
            rig.bin.park(0, std::unique_ptr<mu_core::RetireTail>(t), 0, false);
            juce::AudioBuffer<float> all(2, 256 * 20);
            int at = 0;
            for (int k = 0; k < 20 && rig.bin.stateOf(0, 0) != Bin::Done; ++k)
            {
                rig.block(0, 256, 0);
                all.copyFrom(0, at, rig.out, 0, 0, 256);
                at += 256;
            }
            expect(rig.bin.stateOf(0, 0) == Bin::Done);
            expectEquals(at, 256 * 19, "ended in the block that holds frame 4800");
            bool shape = true;
            const int declick = 480;
            for (int p = 0; p < at; ++p)
            {
                const double g = juce::jlimit(0.0, 1.0, (double) (4800 - p) / (double) declick);
                const float expected = p < 4800 - declick - 256 ? 1.0f : (float) g * 1.0f;
                if (p < 4800 - declick - 256) shape &= all.getSample(0, p) == 1.0f;
                else if (p >= 4800 - declick) shape &= std::abs(all.getSample(0, p) - expected) < 1.0e-6f;
            }
            expect(shape, "full level, then a linear ramp to zero over the last 480 frames of life");
            expect(all.getSample(0, 4799) > 0.0f && all.getSample(0, 4800) == 0.0f);
        }

        beginTest("A lifetime longer than 30 s is clamped to the hard cap");
        {
            BinRig rig(48000.0, 256);
            rig.bin.park(0, std::unique_ptr<mu_core::RetireTail>(new ProbeTail({ mu_core::RetirePolicy::Kind::Natural, 25.0f, 100.0f })), 0, false);
            expectEquals((double) rig.bin.stateDataOf(0, 0).maxSamples, 30.0 * 48000.0);
        }

        beginTest("The activation serial gives the same-block hand-over, across the serial wrap");
        {
            BinRig rig;
            auto* t = new ProbeTail();
            rig.bin.park(0, std::unique_ptr<mu_core::RetireTail>(t), 0xFFFFFFFFu, false);
            rig.block(0, 64, 0xFFFFFFFEu);
            expect(t->starts == 0, "the old view renders the layer live and skips the entry");
            rig.block(0, 64, 0xFFFFFFFFu);
            expect(t->starts == 1, "the first block of the new view renders it as a tail");
            rig.block(0, 64, 2u);
            expectEquals(t->renders, 2, "still rendering after the serial wrapped");
            auto* u = new ProbeTail();
            rig.bin.park(1, std::unique_ptr<mu_core::RetireTail>(u), 1u, false);
            rig.block(1, 64, 0xFFFFFFFFu);
            expect(u->starts == 0, "activation 1 has not been reached by serial 0xFFFFFFFF");
            rig.block(1, 64, 1u);
            expect(u->starts == 1);
        }

        beginTest("cutAll at prepare and release frees everything");
        {
            auto pBox = std::make_unique<FakeLayerHost>(2); auto& p = *pBox;
            Driver d(p);
            const int before = ProbeTail::destroyed();
            p.retireBin.park(1, std::unique_ptr<mu_core::RetireTail>(new ProbeTail()), 0, false);
            p.retireBin.park(4, std::unique_ptr<mu_core::RetireTail>(new ProbeTail()), 0, false);
            p.prepareToPlay(48000.0, 512);
            expectEquals(ProbeTail::destroyed() - before, 2);
            p.retireBin.park(2, std::unique_ptr<mu_core::RetireTail>(new ProbeTail()), 0, false);
            p.releaseResources();
            expectEquals(ProbeTail::destroyed() - before, 3);
        }
    }
};
static RetireBinDepthTests sRetireBinDepthTests;

struct RetireBinRulesTests : juce::UnitTest
{
    RetireBinRulesTests() : juce::UnitTest("RetireBin rules R1 to R4 and R7", "mu-core") {}

    void runTest() override
    {
        beginTest("R1: a second parkInPlace on an active in-place row is a no-op (the running fade continues)");
        {
            BinRig rig;
            ProbeTail spare({ mu_core::RetirePolicy::Kind::Fade, 25.0f, 2.0f });
            expect(rig.bin.parkInPlace(2, spare, 0, false) == Bin::ParkResult::Parked);
            rig.block(2, 256, 0);
            const int left = rig.bin.stateDataOf(2, 0).samplesLeft;
            expect(rig.bin.parkInPlace(2, spare, 0, false) == Bin::ParkResult::AlreadyActive);
            expectEquals(rig.bin.stateDataOf(2, 0).samplesLeft, left);
            rig.bin.cutAll();
            rig.bin.service();
        }

        beginTest("R1: an in-place entry is skipped while its slot is live; regrow (count first, then cut) leaves no block with neither");
        {
            BinRig rig;
            ProbeTail spare({ mu_core::RetirePolicy::Kind::Fade, 25.0f, 2.0f });
            rig.bin.parkInPlace(2, spare, 0, false);
            int neither = 0, both = 0;
            auto oneBlock = [&](bool slotLive)
            {
                const int before = spare.renders;
                rig.block(2, 64, 0, slotLive);
                const bool tail = spare.renders != before;
                if (! tail && ! slotLive) ++neither;
                if (tail && slotLive) ++both;
            };
            oneBlock(false);              // shrunk: the spare fades
            oneBlock(false);
            oneBlock(true);               // regrow: the count store lands first, the slot is live again
            rig.bin.cutSlot(2);           // then the cut
            oneBlock(true);
            expectEquals(neither, 0);
            expectEquals(both, 0);
            rig.bin.service();
            // Shrink, regrow, shrink: the second fade starts fresh, never the stale first one (today's latent bug).
            expect(rig.bin.parkInPlace(2, spare, 0, false) == Bin::ParkResult::Parked);
            int fresh = -1;
            for (int e = 0; e < Bin::kDepth; ++e)
                if (rig.bin.stateOf(2, e) == Bin::Parked) fresh = rig.bin.stateDataOf(2, e).samplesLeft;
            expectEquals(fresh, juce::jmax(1, (int) (48000.0 * 0.025)));
            rig.bin.cutAll();
            rig.bin.service();
        }

        beginTest("R1: a spare is never destroyed before its cut (addLayer over a ringing spare)");
        {
            struct SpareProbe : FakeBeatLayer
            {
                mu_core::RetirePolicy retirePolicy() const override { return { mu_core::RetirePolicy::Kind::Fade, 25.0f, 2.0f }; }
                bool renderRetiring(const mu_core::RenderContext&, juce::AudioBuffer<float>&, int, mu_core::RetireState&) override { return true; }
                ~SpareProbe() override
                {
                    if (bin != nullptr)
                        for (int e = 0; e < Bin::kDepth; ++e)
                            if (bin->tailOf(2, e) == this && (bin->stateOf(2, e) == Bin::Parked || bin->stateOf(2, e) == Bin::Draining))
                                ++(*liveAtDeath);
                }
                const Bin* bin = nullptr;
                int* liveAtDeath = nullptr;
            };
            int liveAtDeath = 0;
            auto pBox = std::make_unique<FakeLayerHost>(2); auto& p = *pBox;
            Driver d(p);
            auto spare = std::make_unique<SpareProbe>();
            spare->bin = &p.retireBin;
            spare->liveAtDeath = &liveAtDeath;
            auto* sp = spare.get();
            {
                const juce::ScopedLock sl(p.layersLock);
                p.layers[2] = std::move(spare);
                p.retireBin.parkInPlace(2, *sp, p.layoutSerial(), true);
            }
            expectEquals(p.addLayer(), 2);   // publishes a fresh layer over the spare and releases the spare
            expectEquals(liveAtDeath, 0, "the in-place entry was cut under the lock before the spare died");
            p.flushPendingAsyncUpdates();
        }

        beginTest("R1: swapRows and removeRow move owned tails with their slot and cut in-place ones");
        {
            BinRig rig;
            auto* o1 = new ProbeTail(); auto* o3 = new ProbeTail();
            ProbeTail spare1;
            rig.bin.park(1, std::unique_ptr<mu_core::RetireTail>(o1), 0, false);
            rig.bin.parkInPlace(1, spare1, 0, false);
            rig.bin.park(3, std::unique_ptr<mu_core::RetireTail>(o3), 0, false);
            rig.bin.swapRows(1, 3);
            auto findIn = [&](int slot, const mu_core::RetireTail* t)
            {
                for (int e = 0; e < Bin::kDepth; ++e) if (rig.bin.tailOf(slot, e) == t) return e;
                return -1;
            };
            expect(findIn(3, o1) >= 0 && findIn(1, o3) >= 0, "owned tails followed their slot");
            const int sp = findIn(3, &spare1) >= 0 ? findIn(3, &spare1) : findIn(1, &spare1);
            expect(sp >= 0);
            expect(rig.bin.stateOf(findIn(3, &spare1) >= 0 ? 3 : 1, sp) == Bin::Done, "the in-place entry was cut");

            rig.bin.removeRow(1, 4);   // slot 1 (holding o3) removed; rows 2, 3 move down; the cut row lands on 3
            expect(findIn(2, o1) >= 0, "row 3's owned tail moved down to 2");
            const int cutAt = findIn(3, o3);
            expect(cutAt >= 0 && rig.bin.stateOf(3, cutAt) == Bin::Done, "the removed row was cut and moved to the vacated top");
            rig.bin.cutAll();
            rig.bin.service();
        }

        beginTest("R2: service, parks and a host thread's prepare / cutAll / service never overlap (binMutex)");
        {
            constexpr int kRounds = 1500;
            mu_core::AudioEpoch epoch;
            Bin bin(epoch);
            bin.setPrepared(48000.0, 256);
            const int before = ProbeTail::destroyed(), outside = ProbeTail::freedOutsideBin();
            std::thread hostThread([&]
            {
                for (int i = 0; i < kRounds; ++i) { bin.setPrepared(i % 2 ? 48000.0 : 44100.0, 256); bin.cutAll(); bin.service(); }
            });
            for (int i = 0; i < kRounds; ++i)
            {
                bin.park(i % mu_limits::kMaxLayers, std::unique_ptr<mu_core::RetireTail>(new ProbeTail()), 0, false);
                bin.service();
            }
            hostThread.join();
            bin.cutAll();
            bin.service();
            expectEquals(ProbeTail::destroyed() - before, kRounds, "every tail freed exactly once");
            expectEquals(ProbeTail::freedOutsideBin() - outside, 0, "always inside the bin's free path, on a non-audio thread");
        }

        beginTest("R3: CutAtCount cuts the rows a count-reducing store leaves; RenderPastCount keeps them");
        {
            auto qBox = std::make_unique<FakeLayerHost>(4); auto& q = *qBox;
            q.setRetireCountPolicy(Bin::CountPolicy::CutAtCount);
            q.retireBin.park(3, std::unique_ptr<mu_core::RetireTail>(new ProbeTail()), 0, false);
            auto state = q.captureComposedState();
            state.setProperty("numVoices", 2, nullptr);
            q.applyComposedState(state);
            expect(q.retireBin.stateOf(3, 0) == Bin::Done);

            auto rBox = std::make_unique<FakeLayerHost>(4); auto& r = *rBox;
            r.retireBin.park(3, std::unique_ptr<mu_core::RetireTail>(new ProbeTail()), 0, false);
            r.applyComposedState(state);
            expect(r.retireBin.stateOf(3, 0) == Bin::Parked);
        }

        beginTest("R4: a failed LayerBlock changes no entry; an entry cut after phase 0 renders silence, not last block's samples");
        {
            auto pBox = std::make_unique<FakeLayerHost>(1); auto& p = *pBox;
            p.setUseLayerRenderForTest(true);
            AppendTap tap(8192);
            p.setChannelTapForTest(&tap);
            Driver d(p);
            auto* t = new ProbeTail();
            t->value = 0.5f;
            p.retireBin.park(3, std::unique_ptr<mu_core::RetireTail>(t), p.layoutSerial(), false);
            {
                struct H { juce::CriticalSection& l; juce::WaitableEvent in, out; std::thread th;
                           explicit H(juce::CriticalSection& c) : l(c), th([this] { l.enter(); in.signal(); out.wait(); l.exit(); }) { in.wait(); }
                           ~H() { out.signal(); th.join(); } } h(p.layersLock);
                d.block(256);
            }
            expect(p.retireBin.stateOf(3, 0) == Bin::Parked && t->starts == 0, "the missed block touched no entry");
            d.block(256);
            expect(p.retireBin.stateOf(3, 0) == Bin::Draining);
            expect(tap.bufs[3].getMagnitude(0, 0, tap.written[3]) > 0.0f, "the tail sounds in its own slot past the count");

            // A cut that lands while the block runs (after mixCount was taken): slot 0's render performs it.
            struct Cutter : FakeBeatLayer
            {
                void render(const mu_core::RenderContext& ctx, juce::AudioBuffer<float>& out, int n) override
                {
                    FakeBeatLayer::render(ctx, out, n);
                    if (bin != nullptr) { bin->cutSlot(3); bin = nullptr; }
                }
                Bin* bin = nullptr;
            };
            auto cutter = std::make_unique<Cutter>();
            cutter->bin = &p.retireBin;
            {
                const juce::ScopedLock sl(p.layersLock);
                p.layers[0] = std::move(cutter);
            }
            const int at = tap.written[3];
            d.block(256);
            expectEquals(p.getLayerRenderProbe().mixCount, 4, "mixCount was taken with the entry active");
            expectEquals(tap.bufs[3].getMagnitude(0, at, 256), 0.0f, "the slot rendered silence, never stale scratch");
            p.setChannelTapForTest(nullptr);
        }

        beginTest("R7: a tail emits its owed note-off into the reserve even when live layers filled their sink");
        {
            struct Flood : FakeBeatLayer
            {
                void render(const mu_core::RenderContext& ctx, juce::AudioBuffer<float>&, int) override
                { while (ctx.emitMidi(0, 0x90, 64, 100)) ++emitted; }
                int emitted = 0;
            };
            auto pBox = std::make_unique<FakeLayerHost>(1); auto& p = *pBox;
            p.setUseLayerRenderForTest(true);
            Driver d(p);
            {
                const juce::ScopedLock sl(p.layersLock);
                p.layers[0] = std::make_unique<Flood>();
            }
            auto* t = new ProbeTail();
            t->owesNoteOff = true;
            p.retireBin.park(0, std::unique_ptr<mu_core::RetireTail>(t), p.layoutSerial(), false);
            d.block(256);
            expect(static_cast<Flood&>(*p.layers[0]).emitted > 1000, "the live sink was filled");
            expectEquals(t->starts, 1);
            juce::MidiBuffer out;
            out.ensureSize(64);
            expectEquals(p.mergeLayerMidiOut(out, 9), 1, "a budget of one event takes the owed note-off first");
            for (const auto m : out) expect(m.getMessage().isNoteOff());
        }

        beginTest("Destruction: the bin is empty before any product member a tail might read is destroyed");
        {
            static int liveTails = 0;
            static int aliveAtDeath = -1;
            struct CountedTail : ProbeTail { CountedTail() { ++liveTails; } ~CountedTail() override { --liveTails; } };
            struct Service { ~Service() { aliveAtDeath = liveTails; } };   // stands for a wavetable bank / sample library
            struct Host : FakeLayerHost { Service svc; Host() : FakeLayerHost(1) {} ~Host() override { prepareForDestruction(); } };
            {
                auto hBox = std::make_unique<Host>(); auto& h = *hBox;
                h.retireBin.park(0, std::unique_ptr<mu_core::RetireTail>(new CountedTail()), 0, false);
                expectEquals(liveTails, 1);
            }
            expectEquals(aliveAtDeath, 0);
        }
    }
};
static RetireBinRulesTests sRetireBinRulesTests;

// ─── The review's M3 and M4: no free or wait under binMutex; a regrow by state cuts in-place tails ────
namespace
{
    // A spare that fades in place (mu-Tant's shape), renders a constant into its tail.
    struct FadingSpare : FakeBeatLayer
    {
        mu_core::RetirePolicy retirePolicy() const override { return { mu_core::RetirePolicy::Kind::Fade, 25.0f, 2.0f }; }
        bool renderRetiring(const mu_core::RenderContext&, juce::AudioBuffer<float>& out, int n, mu_core::RetireState& rs) override
        {
            for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::add(out.getWritePointer(c), 0.25f, n);
            mu_core::applyLinearFade(out, n, rs);
            return true;
        }
    };
}

struct RetireBinReviewTests : juce::UnitTest
{
    RetireBinReviewTests() : juce::UnitTest("RetireBin review fixes (M3 lock hold, M4 regrow by state)", "mu-core") {}

    void runTest() override
    {
        beginTest("M4: applyComposedState shrink 4 to 2, regrow to 4 inside the fade, shrink again: the second park fades afresh");
        {
            auto pBox = std::make_unique<FakeLayerHost>(4); auto& p = *pBox;
            {
                const juce::ScopedLock sl(p.layersLock);
                for (int s = 2; s < 4; ++s) { p.layers[(size_t) s] = std::make_unique<FadingSpare>(); p.layers[(size_t) s]->bindParams(p.apvts, s); }
            }
            p.setUseLayerRenderForTest(true);
            Driver d(p);
            auto state4 = p.captureComposedState();
            auto state2 = state4.createCopy();
            state2.setProperty("numVoices", 2, nullptr);
            auto shrink = [&]
            {
                // mu-Tant's E4.6 shape: park in place with the next serial, the count store in the same layout hold.
                const juce::ScopedLock lw(p.layoutWriteLock);
                const auto next = p.nextLayoutSerial();
                const auto r2 = p.retireBin.parkInPlace(2, *p.layers[2], next, false);
                const auto r3 = p.retireBin.parkInPlace(3, *p.layers[3], next, false);
                p.applyComposedState(state2);
                return std::make_pair(r2, r3);
            };
            const auto first = shrink();
            expect(first.first == Bin::ParkResult::Parked && first.second == Bin::ParkResult::Parked);
            d.block(256);
            expect(p.retireBin.countIn(Bin::Draining) == 2, "both spares fade");
            p.applyComposedState(state4);   // regrow inside the 25 ms fade
            expectEquals(p.retireBin.countIn(Bin::Draining) + p.retireBin.countIn(Bin::Parked), 0,
                         "the regrow cut the in-place entries (count first, then cut)");
            d.block(256);
            p.flushPendingAsyncUpdates();
            p.retireBin.service();
            const auto second = shrink();
            expect(second.first == Bin::ParkResult::Parked && second.second == Bin::ParkResult::Parked,
                   "the second shrink parks again (not AlreadyActive: no stale fade stays armed)");
            int fresh = 0;
            for (int e = 0; e < Bin::kDepth; ++e)
                if (p.retireBin.stateOf(2, e) == Bin::Parked)
                    fresh = p.retireBin.stateDataOf(2, e).samplesLeft;
            expectEquals(fresh, juce::jmax(1, (int) (48000.0 * 0.025)), "the new fade starts from its full ramp");
        }

        beginTest("M3: forced evictions, structural edits and rendering on three threads; nothing is freed or waited on under binMutex");
        {
            constexpr int kParks = 1500, kEdits = 200, kBlocks = 5000;
            auto pBox = std::make_unique<FakeLayerHost>(3); auto& p = *pBox;
            p.setUseLayerRenderForTest(true);
            p.retireBin.setEvictTimeoutMs(5);
            Driver d(p);
            const int underMutexBefore = ProbeTail::freedUnderMutex(), uafBefore = ProbeTail::useAfterFree();
            std::atomic<bool> stop { false };
            std::atomic<int> longestMissRun { 0 };
            std::thread audio([&]
            {
                juce::AudioBuffer<float> buf(2, 256);
                juce::MidiBuffer m;
                mu_core::BlockTransport t;
                t.playing = true; t.bpm = 120.0; t.beatsPerSample = 2.0 / 48000.0; t.blockBeats = t.beatsPerSample * 256;
                int run = 0;
                for (int i = 0; i < kBlocks; ++i)
                {
                    const int skippedBefore = p.getLayerRenderProbe().blocksSkipped;
                    p.renderAndMixForTest(buf, m, t);
                    run = p.getLayerRenderProbe().blocksSkipped != skippedBefore ? run + 1 : 0;
                    if (run > longestMissRun.load()) longestMissRun.store(run);
                    t.startBeat += t.blockBeats;
                }
                stop.store(true);
            });
            std::thread parker([&]
            {
                for (int i = 0; i < kParks; ++i)
                {
                    // Five into a row of four: every fifth park evicts entry 0, outside layersLock (a wait, unlocked).
                    auto* tail = new ProbeTail({ mu_core::RetirePolicy::Kind::Natural, 25.0f, 0.05f });
                    p.retireBin.park(i % 2, std::unique_ptr<mu_core::RetireTail>(tail), p.layoutSerial(), false);
                    if (i % 7 == 0) p.retireBin.service();
                }
            });
            for (int e = 0; e < kEdits; ++e)
            {
                switch (e % 3)
                {
                    case 0:  p.addLayer(); break;
                    case 1:  if (p.getNumLayers() > 1) p.removeLayer(p.getNumLayers() - 1); break;
                    default: if (p.getNumLayers() > 1) p.swapLayers(0, p.getNumLayers() - 1); break;
                }
                p.flushPendingAsyncUpdates();
            }
            parker.join();
            audio.join();
            p.retireBin.cutAll();
            p.retireBin.service();
            expectEquals(ProbeTail::freedUnderMutex() - underMutexBefore, 0, "no tail destructor ran while its thread held binMutex");
            expectEquals(ProbeTail::useAfterFree() - uafBefore, 0, "no tail rendered after it was freed");
            logMessage("lock-hold stress: longest run of skipped blocks " + juce::String(longestMissRun.load())
                       + " (edits raise layerEditInFlight; information)");
        }
    }
};
static RetireBinReviewTests sRetireBinReviewTests;

// ─── C3: frozen retire snapshots, the isolation twin test ────────────────────────────────────────────
namespace
{
    struct SnapValues { float gain; };

    // A layer whose tail reads only its snapshot (taken in beginRetire) and the context's beat.
    struct SnapLayer : FakeBeatLayer
    {
        mu_core::RetirePolicy retirePolicy() const override { return { mu_core::RetirePolicy::Kind::Fade, 25.0f, 2.0f }; }
        void beginRetire() override { snap.value.gain = gainParam.load(); }
        bool renderRetiring(const mu_core::RenderContext& ctx, juce::AudioBuffer<float>& out, int n, mu_core::RetireState& rs) override
        {
            pathSeen = pathSeen && inRetirePath();
            renderFakeEngine(absoluteFrame(ctx) + 7, ctx.slot, scratch, n, liveRead ? 0.25f + rawGain->load() : snap.value.gain);
            mu_core::applyLinearFade(scratch, n, rs);
            for (int c = 0; c < 2; ++c) out.addFrom(c, 0, scratch, c, 0, n);
            for (int i = 0; i < n && captured < capture.getNumSamples(); ++i) capture.setSample(0, captured++, scratch.getSample(0, i));
            return true;
        }
        mu_core::RetireSnapshot<SnapValues> snap;
        juce::AudioBuffer<float> scratch { 2, 512 }, capture { 1, 48000 };
        int captured = 0;
        bool pathSeen = true;
        bool liveRead = false;              // the negative control: reads the live parameter (forbidden)
        std::atomic<float>* rawGain = nullptr;
    };

    // Renders one twin: blocks, then the slot-1 layer retires (in place, the count shrinks), then (on the
    // scrambled twin) every parameter is randomised before the tail renders.
    std::vector<float> renderTwin(bool scramble, bool liveRead, bool samePrefixReplacement)
    {
        auto pBox = std::make_unique<FakeLayerHost>(2); auto& p = *pBox;
        {
            const juce::ScopedLock sl(p.layersLock);
            auto l = std::make_unique<SnapLayer>();
            l->bindParams(p.apvts, 1);
            l->liveRead = liveRead;
            l->rawGain = p.apvts.getRawParameterValue("v1_a");
            p.layers[1] = std::move(l);
        }
        p.apvts.getParameter("v1_a")->setValueNotifyingHost(0.6f);
        p.setUseLayerRenderForTest(true);
        Driver d(p);
        for (int i = 0; i < 8; ++i) d.block(256);

        auto* old = static_cast<SnapLayer*>(p.layers[1].get());
        if (! samePrefixReplacement)
        {
            // mu-Tant's shape: snapshot, park in place with the next serial, then the count store, in one layout hold.
            const juce::ScopedLock lw(p.layoutWriteLock);
            old->beginRetire();
            p.retireBin.parkInPlace(1, *old, p.nextLayoutSerial(), false);
            p.storeLayerCount(1);
        }
        else
        {
            // A same-prefix type takes the slot while the old one rings: both read v1_a, only the new one may.
            const juce::ScopedLock lw(p.layoutWriteLock);
            const juce::ScopedLock sl(p.layersLock);
            old->beginRetire();
            auto owned = std::move(p.layers[1]);
            auto fresh = std::make_unique<FakeBeatLayer>();
            fresh->bindParams(p.apvts, 1);
            p.layers[1] = std::move(fresh);
            p.retireBin.park(1, std::move(owned), p.layoutSerial(), true);
        }

        if (scramble)
        {
            juce::Random r(0xC3);
            for (auto* param : p.getParameters()) param->setValueNotifyingHost(r.nextFloat());
            p.mixerEngine.channels[1].panMod.store(0.7f);   // and the strip state the mixer reads
        }
        for (int i = 0; i < 8; ++i) d.block(256);

        std::vector<float> out;
        for (int e = 0; e < Bin::kDepth; ++e)
            if (auto* t = const_cast<mu_core::RetireTail*>(p.retireBin.tailOf(1, e)))
                if (auto* s = dynamic_cast<SnapLayer*>(t))
                {
                    for (int i = 0; i < s->captured; ++i) out.push_back(s->capture.getSample(0, i));
                    if (! s->pathSeen) out.push_back(1.0e9f);   // the retire-path flag was off in a tail call
                }
        return out;
    }
}

struct RetireIsolationTests : juce::UnitTest
{
    RetireIsolationTests() : juce::UnitTest("Retire isolation twin test (C3, N6)", "mu-core") {}

    void runTest() override
    {
        beginTest("A tail renders the same with every parameter scrambled (tail's own output, before the strip)");
        {
            const auto plain = renderTwin(false, false, false);
            const auto scrambled = renderTwin(true, false, false);
            expect(! plain.empty(), "the tail rendered");
            expect(plain == scrambled, "the tail read nothing but its snapshot");
        }

        beginTest("The same-prefix case: a new layer on the same prefix while the old one rings");
        {
            const auto plain = renderTwin(false, false, true);
            const auto scrambled = renderTwin(true, false, true);
            expect(! plain.empty());
            expect(plain == scrambled);
        }

        beginTest("Negative control: a tail that reads its live parameter is caught by the twin test");
        {
            const auto plain = renderTwin(false, true, false);
            const auto scrambled = renderTwin(true, true, false);
            expect(plain != scrambled);
        }
    }
};
static RetireIsolationTests sRetireIsolationTests;
