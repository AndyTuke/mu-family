// The layer render contract's audio half (E1): RenderContext, the time model, AudioEpoch, LayerBlock in both
// modes, and ProcessorBase's block driver (renderAndMix / renderLayers) proven on fake layer types.
// docs/mu-tronic/design.md 2.1.1 to 2.1.5; extraction-plan 2.1 (C1, C4, C8 to C11) and tests.md section 6.
//
// Threads: every multi-thread test uses fixed iteration counts and asserts no timing.

#include "AllocationProbe.h"   // the counting operators are in MidiControlTests.cpp (one TU per executable)
#include "LayerContractFixtures.h"
#include "Sequencer/BeatWrap.h"

#include <cstddef>
#include <thread>
#include <vector>

using namespace mu_contract_test;

namespace
{
    // A second thread that holds a lock, or sits inside an epoch "block", until released.
    struct Holder
    {
        juce::WaitableEvent entered, release;
        std::thread t;
        template <class Enter, class Leave>
        Holder(Enter enter, Leave leave)
            : t([this, enter, leave] { enter(); entered.signal(); release.wait(); leave(); })
        { entered.wait(); }
        ~Holder() { release.signal(); t.join(); }
    };

    // Renders `total` frames through the shared driver with the given block schedule (repeated) and returns
    // the master output.
    juce::AudioBuffer<float> renderThrough(FakeLayerHost& p, const std::vector<int>& blocks, int total,
                                           double startBeat = 0.0, AppendTap* tap = nullptr, int* allocs = nullptr)
    {
        p.setUseLayerRenderForTest(true);
        p.setChannelTapForTest(tap);
        Driver d(p);
        d.host.ppq = startBeat;
        d.countAllocations = allocs != nullptr;
        juce::AudioBuffer<float> out(2, total);
        int at = 0;
        for (size_t k = 0; at < total; ++k)
        {
            const int n = juce::jmin(blocks[k % blocks.size()], total - at);
            const auto& b = d.block(n);
            for (int c = 0; c < 2; ++c) out.copyFrom(c, at, b, c, 0, n);
            at += n;
        }
        if (allocs != nullptr) *allocs = d.allocations + d.frees;
        p.setChannelTapForTest(nullptr);
        return out;
    }

    bool bitEqual(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int n, int* firstDiff = nullptr)
    {
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
                if (std::memcmp(a.getReadPointer(c) + i, b.getReadPointer(c) + i, sizeof (float)) != 0)
                {
                    if (firstDiff != nullptr) *firstDiff = i;
                    return false;
                }
        return true;
    }

    float peakOf(const juce::AudioBuffer<float>& b, int n)
    {
        float m = 0.0f;
        for (int c = 0; c < b.getNumChannels(); ++c) m = juce::jmax(m, b.getMagnitude(c, 0, n));
        return m;
    }
}

// ─── RenderContext: the C-struct mirror ──────────────────────────────────────────────────────────────
struct RenderContextLayoutTests : juce::UnitTest
{
    RenderContextLayoutTests() : juce::UnitTest("RenderContext layout freeze (C4, A14 constraints)", "mu-core") {}

    void runTest() override
    {
        using mu_core::RenderContext;
        beginTest("standard layout, trivially copyable, fixed size and offsets, no implicit padding");
        expect(std::is_standard_layout_v<RenderContext> && std::is_trivially_copyable_v<RenderContext>);
       #if JUCE_64BIT
        expectEquals((int) sizeof (mu_core::MidiEvent), 8);
        expectEquals((int) sizeof (mu_core::MidiView), 24);
        expectEquals((int) sizeof (mu_core::MidiSink), 24);
        expectEquals((int) sizeof (mu_core::PeerTap), 24);
        expectEquals((int) sizeof (RenderContext), 144);
        // The frozen offsets: a later C header would mirror exactly these.
        expectEquals((int) offsetof(RenderContext, structSize), 0);
        expectEquals((int) offsetof(RenderContext, version), 4);
        expectEquals((int) offsetof(RenderContext, sampleRate), 8);
        expectEquals((int) offsetof(RenderContext, blockSize), 16);
        expectEquals((int) offsetof(RenderContext, preparedBlockSize), 20);
        expectEquals((int) offsetof(RenderContext, frameOffset), 24);
        expectEquals((int) offsetof(RenderContext, slot), 28);
        expectEquals((int) offsetof(RenderContext, bpm), 32);
        expectEquals((int) offsetof(RenderContext, startBeat), 40);
        expectEquals((int) offsetof(RenderContext, beatsPerSample), 48);
        expectEquals((int) offsetof(RenderContext, callBeats), 56);
        expectEquals((int) offsetof(RenderContext, beatWrap), 64);
        expectEquals((int) offsetof(RenderContext, playing), 72);
        expectEquals((int) offsetof(RenderContext, justStarted), 73);
        expectEquals((int) offsetof(RenderContext, justStopped), 74);
        expectEquals((int) offsetof(RenderContext, transportJumped), 75);
        expectEquals((int) offsetof(RenderContext, retiring), 76);
        expectEquals((int) offsetof(RenderContext, frozen), 77);
        expectEquals((int) offsetof(RenderContext, stoppedUnrendered), 78);
        expectEquals((int) offsetof(RenderContext, reserved), 79);
        expectEquals((int) offsetof(RenderContext, midi), 80);
        expectEquals((int) offsetof(RenderContext, midiOut), 104);
        expectEquals((int) offsetof(RenderContext, peers), 128);
        expectEquals((int) offsetof(RenderContext, numPeers), 136);
        expectEquals((int) offsetof(RenderContext, reserved2), 140);
        // The sum of the members is the size: no byte of the struct is padding that could carry state.
        const size_t members = 4 + 4 + 8 + 4 * 4 + 8 * 5 + 6 + 2 + sizeof (mu_core::MidiView)
                             + sizeof (mu_core::MidiSink) + sizeof (void*) + 4 + 4;
        expectEquals((int) members, (int) sizeof (RenderContext));
        expectEquals((int) offsetof(mu_core::MidiView, carried), 20);
        expectEquals((int) offsetof(mu_core::MidiView, inputReset) + 2, (int) sizeof (mu_core::MidiView));
        expectEquals((int) offsetof(mu_core::MidiSink, count), 16);
        expectEquals((int) offsetof(mu_core::PeerTap, valid), 16);
       #endif

        beginTest("makeRenderContext fills the header; peer() rejects absent, invalid and out-of-range taps");
        auto c = mu_core::makeRenderContext();
        expectEquals((int) c.structSize, (int) sizeof (RenderContext));
        expectEquals((int) c.version, (int) mu_core::kRenderContextVersion);
        expect(c.peer(0) == nullptr);
        std::array<mu_core::PeerTap, 2> taps {};
        taps[1].valid = 1;
        c.peers = taps.data();
        c.numPeers = 2;
        expect(c.peer(0) == nullptr && c.peer(1) == &taps[1] && c.peer(2) == nullptr && c.peer(-1) == nullptr);

        beginTest("forEachMidi gives the call's own frames, relative to the call, honouring the channel mask");
        std::array<mu_core::MidiEvent, 4> ev {};
        ev[0] = { 3,   3, { 0x90, 60, 100 } };   // channel 1, before the call
        ev[1] = { 10,  3, { 0x91, 61, 100 } };   // channel 2, inside
        ev[2] = { 12,  3, { 0x90, 62, 100 } };   // channel 1, inside
        ev[3] = { 20,  1, { 0xF8, 0, 0 } };      // clock (no channel), at the end (outside)
        c.midi = { ev.data(), 4, 8, 0xFFFF, 0, 0 };
        c.blockSize = 12;
        c.frameOffset = 8;
        std::vector<int> frames;
        mu_core::forEachMidi(c, [&](const mu_core::MidiEvent&, int f) { frames.push_back(f); });
        expect(frames == std::vector<int> { 2, 4 });
        c.midi.channelMask = 0x0001;   // channel 1 only
        frames.clear();
        mu_core::forEachMidi(c, [&](const mu_core::MidiEvent&, int f) { frames.push_back(f); });
        expect(frames == std::vector<int> { 4 });
    }
};
static RenderContextLayoutTests sRenderContextLayoutTests;

// ─── The beat arithmetic ─────────────────────────────────────────────────────────────────────────────
struct BeatWrapTests : juce::UnitTest
{
    BeatWrapTests() : juce::UnitTest("Beat wrap and the continuity reduction (R6, F1)", "mu-core") {}

    void runTest() override
    {
        using mu_core::wrapBeat;
        beginTest("wrapBeat is the old resolver lambda on non-negative input (bit for bit)");
        auto oldLambda = [](double b, double w) { if (w <= 0.0) return b; b = std::fmod(b, w); return b < 0.0 ? b + w : b; };
        juce::Random r(0x5eed);
        for (int i = 0; i < 20000; ++i)
        {
            const double b = r.nextDouble() * 1.0e6 * (i % 3 == 0 ? 1.0e-3 : 1.0);
            for (double w : { 0.0, 4.0, 16.0, 64.0, 7.5 })
            {
                const double a = wrapBeat(b, w), o = oldLambda(b, w);
                if (std::memcmp(&a, &o, sizeof a) != 0) { expect(false, "wrapBeat differs on " + juce::String(b, 17)); return; }
            }
        }

        beginTest("negative beats wrap upward and never land on the bound (pre-roll)");
        expectEquals(wrapBeat(-4.0, 64.0), 60.0);
        expectEquals(wrapBeat(-64.0, 64.0), 0.0);
        const double tiny = wrapBeat(-1.0e-17, 64.0);
        expect(tiny < 64.0 && tiny >= 0.0, "a tiny negative beat stays inside [0, 64)");
        expectEquals(wrapBeat(-4.0, 0.0), -4.0);   // unbounded: unchanged

        beginTest("the jump check's delta reduction at beatWrap 64: +-1e-4, +-1e-4 +- 64 and +-64 are not jumps");
        const double tol = 1.0 / 256.0;
        for (double d : { 1.0e-4, -1.0e-4, 1.0e-4 + 64.0, 1.0e-4 - 64.0, -1.0e-4 + 64.0, -1.0e-4 - 64.0, 64.0, -64.0 })
            expect(std::abs(mu_core::reduceBeatDelta(d, 64.0)) <= tol, "delta " + juce::String(d) + " read as a jump");
        expect(std::abs(mu_core::reduceBeatDelta(1.0, 64.0)) > tol, "a one-beat locate is a jump");
        // wrapBeat would have turned -1e-4 into 63.9999: the reason the reduction rounds to the nearest multiple.
        expect(wrapBeat(-1.0e-4, 64.0) > 63.0);
    }
};
static BeatWrapTests sBeatWrapTests;

// ─── AudioEpoch ──────────────────────────────────────────────────────────────────────────────────────
struct AudioEpochTests : juce::UnitTest
{
    AudioEpochTests() : juce::UnitTest("AudioEpoch grace period (C1, N2, N4)", "mu-core") {}

    void runTest() override
    {
        beginTest("enter / leave keep parity; a stamp outside a block has passed at once, inside only after leave");
        mu_core::AudioEpoch e;
        const auto s0 = e.stamp();
        expect((s0 & 1u) == 0 && e.passed(s0));
        e.enter();
        const auto s1 = e.stamp();
        expect((s1 & 1u) == 1 && ! e.passed(s1), "a block that started before the stamp may still hold the old pointer");
       #if JUCE_DEBUG
        expect(e.callerIsInsideBlock());
       #else
        expect(! e.callerIsInsideBlock());
       #endif
        e.leave();
        expect(e.passed(s1) && ! e.callerIsInsideBlock());
        expect(! e.reclaimDisabled());

        beginTest("waitFor: true once the other thread's block ends, false after the timeout while it runs (N2)");
        {
            Holder inside([&] { e.enter(); }, [&] { e.leave(); });
            const auto s = e.stamp();
            expect(! e.waitFor(s, 20), "a block that never ends times the waiter out");
            expect(! e.passed(s));
        }
        expect(e.passed(e.stamp()) && e.waitFor(e.stamp(), 0));
        {
            mu_core::AudioEpoch e2;
            juce::WaitableEvent started;
            std::thread t([&] { e2.enter(); started.signal(); juce::Thread::sleep(5); e2.leave(); });
            started.wait();
            const auto s = e2.stamp();
            expect(e2.waitFor(s, 5000), "the waiter returns when the block ends");
            t.join();
        }

        beginTest("a wrong parity turns reclamation off for good (the Debug assert is expected here)");
        {
            mu_core::AudioEpoch bad;
            bad.enter();
            bad.enter();          // two blocks at once: outside the JUCE contract
            expect(bad.reclaimDisabled());
            bad.leave();
            bad.leave();
            expect(bad.reclaimDisabled(), "sticky");
        }

        beginTest("a moved audio thread is still correct (read-modify-write counter)");
        {
            mu_core::AudioEpoch m;
            for (int i = 0; i < 50; ++i)
            {
                std::thread t([&] { mu_core::ScopedAudioEpoch b(m); });
                t.join();
            }
            expect(! m.reclaimDisabled() && (m.stamp() & 1u) == 0 && m.stamp() == 100);
        }
    }
};
static AudioEpochTests sAudioEpochTests;

// ─── LayerBlock and the published views ──────────────────────────────────────────────────────────────
struct LayerBlockTests : juce::UnitTest
{
    LayerBlockTests() : juce::UnitTest("LayerBlock: TryLock and Published access (C1, C11, N4, N5)", "mu-core") {}

    void runTest() override
    {
        beginTest("TryLock: the view is the layout word's count and serial plus every slot, spares included");
        {
            auto pBox = std::make_unique<FakeLayerHost>(3); auto& p = *pBox;
            const auto serial = p.layoutSerial();
            mu_core::LayerBlock b(p.layerAccess, p.layersLock, p.layerEditInFlight, p.layers);
            expect((bool) b);
            expectEquals((int) b.view().numLive, 3);
            expectEquals((int) b.view().serial, (int) serial);
            for (int s = 0; s < mu_limits::kMaxLayers; ++s) expect(b.view().layer[(size_t) s] == p.layers[(size_t) s].get());
            // The lock is held for the life of the block: another thread cannot take it.
            bool other = true;
            std::thread t([&] { other = p.layersLock.tryEnter(); if (other) p.layersLock.exit(); });
            t.join();
            expect(! other, "layersLock is held while the block lives");
           #if JUCE_INTEL
            expect(juce::FloatVectorOperations::areDenormalsDisabled(), "the block flushes denormals (C11)");
           #endif
        }

        beginTest("TryLock: a held layersLock or an edit in flight gives a false block, and the epoch still moves");
        {
            auto pBox = std::make_unique<FakeLayerHost>(2); auto& p = *pBox;
            const auto before = p.layerAccess.epoch.stamp();
            {
                Holder h([&] { p.layersLock.enter(); }, [&] { p.layersLock.exit(); });
                mu_core::LayerBlock b(p.layerAccess, p.layersLock, p.layerEditInFlight, p.layers);
                expect(! (bool) b);
            }
            p.layerEditInFlight.store(true);
            {
                mu_core::LayerBlock b(p.layerAccess, p.layersLock, p.layerEditInFlight, p.layers);
                expect(! (bool) b);
            }
            p.layerEditInFlight.store(false);
            // The failed block released the lock it had taken.
            expect(p.layersLock.tryEnter());
            p.layersLock.exit();
            expectEquals((int) (p.layerAccess.epoch.stamp() - before), 4);
        }

        beginTest("Published: the audio side never touches layersLock and reads the one published view");
        {
            auto pBox = std::make_unique<FakeLayerHost>(2, mu_core::LayerAccess::Published); auto& p = *pBox;
            mu_core::LayerView v;
            v.numLive = 2;
            v.serial = 41;
            v.layer[0] = p.layers[0].get();
            v.layer[1] = p.layers[1].get();
            p.layerAccess.views.publish(v, p.layerAccess.epoch);
            Holder h([&] { p.layersLock.enter(); }, [&] { p.layersLock.exit(); });   // a writer holds the writers' mutex
            {
                mu_core::LayerBlock b(p.layerAccess, p.layersLock, p.layerEditInFlight, p.layers);
                expect((bool) b, "a held layersLock does not stop a Published block");
                expectEquals((int) b.view().serial, 41);
                expect(b.view().layer[1] == p.layers[1].get());
            }
            p.layerEditInFlight.store(true);
            mu_core::LayerBlock quiesced(p.layerAccess, p.layersLock, p.layerEditInFlight, p.layers);
            expect(! (bool) quiesced, "an edit in flight (quiesce) renders nothing");
            p.layerEditInFlight.store(false);
        }

        beginTest("Published: views are reused only after their grace; a stuck block forces counted overflow views");
        {
            mu_core::AudioEpoch e;
            mu_core::LayerViewPublisher pub;
            mu_core::LayerView v;
            for (int i = 0; i < 100; ++i)   // a block between every publication: the pool of four suffices
            {
                v.serial = (uint32_t) i;
                pub.publish(v, e);
                mu_core::ScopedAudioEpoch blk(e);
                expectEquals((int) pub.load()->serial, i);
            }
            expectEquals(pub.overflowCount(), 0);
            {
                Holder inside([&] { e.enter(); }, [&] { e.leave(); });   // a block that never ends
                for (int i = 0; i < 6; ++i) pub.publish(v, e);
                expect(pub.overflowCount() > 0, "the writer allocates instead of waiting");
                expect(pub.viewsInGrace() >= 6);
            }
            pub.reclaim(e);
            expectEquals(pub.viewsInGrace(), 0);
        }

        beginTest("Published two-thread stress: no view or layer is freed while a block can still read it");
        {
            constexpr int kWrites = 3000, kBlocks = 30000;
            auto pBox = std::make_unique<FakeLayerHost>(0, mu_core::LayerAccess::Published); auto& p = *pBox;
            std::atomic<bool> done { false };
            std::atomic<int> uaf { 0 }, rendered { 0 };
            juce::AudioBuffer<float> scratch(2, 16);
            std::thread audio([&]
            {
                for (int i = 0; i < kBlocks; ++i)
                {
                    mu_core::LayerBlock b(p.layerAccess, p.layersLock, p.layerEditInFlight, p.layers);
                    if (! b) continue;
                    for (int s = 0; s < b.view().numLive; ++s)
                        if (auto* l = static_cast<FakeBeatLayer*>(b.view().layer[(size_t) s]))
                        {
                            if (l->magic != kAliveMagic) uaf.fetch_add(1);
                            rendered.fetch_add(1, std::memory_order_relaxed);
                        }
                }
                done.store(true);
            });
            // Writer: each publication swaps slot 0 to a fresh layer; the old one is freed only after its grace.
            std::vector<std::pair<std::unique_ptr<FakeBeatLayer>, uint64_t>> retired;
            std::unique_ptr<FakeBeatLayer> current;
            for (int w = 0; w < kWrites; ++w)
            {
                auto fresh = std::make_unique<FakeBeatLayer>();
                mu_core::LayerView v;
                v.numLive = 1;
                v.serial = (uint32_t) w;
                v.layer[0] = fresh.get();
                {
                    const juce::ScopedLock writers(p.layersLock);
                    p.layerAccess.views.publish(v, p.layerAccess.epoch);
                }
                if (current) retired.push_back({ std::move(current), p.layerAccess.epoch.stamp() });
                current = std::move(fresh);
                for (size_t i = 0; i < retired.size(); )
                    if (p.layerAccess.epoch.passed(retired[i].second)) retired.erase(retired.begin() + (std::ptrdiff_t) i);
                    else ++i;
            }
            audio.join();
            expectEquals(uaf.load(), 0);
            logMessage("published stress: " + juce::String(rendered.load()) + " layer reads, "
                       + juce::String(p.layerAccess.views.overflowCount()) + " overflow views (information)");
            mu_core::LayerView empty;
            p.layerAccess.views.publish(empty, p.layerAccess.epoch);
            p.layerAccess.views.reclaim(p.layerAccess.epoch);
        }
    }
};
static LayerBlockTests sLayerBlockTests;

// ─── The block driver: render order, peers, chunking, edges ──────────────────────────────────────────
struct LayerRenderDriverTests : juce::UnitTest
{
    LayerRenderDriverTests() : juce::UnitTest("Layer render driver: renderAndMix / renderLayers (C1, C4, C8, C10)", "mu-core") {}

    void runTest() override
    {
        beginTest("Chunking (C8): block sizes 1, 7, 64, 512, oversize and ragged render the same samples bit for bit");
        {
            constexpr int total = 48000;
            auto refBox = std::make_unique<FakeLayerHost>(3); auto& ref = *refBox;
            const auto reference = renderThrough(ref, { 512 }, total);
            expect(peakOf(reference, total) > 0.01f, "the fake layers make sound");
            for (const auto& sched : std::vector<std::vector<int>> { { 1 }, { 7 }, { 64 }, { 1500 }, { 4096 },
                                                                     { 1, 7, 63, 64, 129, 512, 1024, 3 } })
            {
                auto pBox = std::make_unique<FakeLayerHost>(3); auto& p = *pBox;
                AppendTap tap(total);
                const auto out = renderThrough(p, sched, total, 0.0, &tap);
                int diff = -1;
                const bool same = bitEqual(out, reference, total, &diff);   // before the message reads diff
                expect(same,
                       "schedule starting " + juce::String(sched[0]) + " differs at frame " + juce::String(diff));
                const auto probe = p.getLayerRenderProbe();
                expect(probe.largestCall <= 512, "no call exceeds the prepared size");
                expect(tap.maxCall <= 512, "the mixer is never handed more than the prepared size (oversize safe)");
                for (int s = 0; s < 3; ++s)
                {
                    expect(p.fake(s).maxCall <= 512 && p.fake(s).contractBreaks == 0,
                           "a layer sees blockSize == n <= prepared and frameOffset == midi.firstFrame");
                }
            }
        }

        beginTest("C8 with the DAW sidechain: every chunk ducks against its own sidechain frames (512 vs 1500, 4096, ragged)");
        {
            constexpr int total = 24000;
            // An impulse every 700 frames on the sidechain input, which shares the output's buffer channels.
            auto renderDucked = [total](const std::vector<int>& sched, bool withSidechain)
            {
                auto pBox = std::make_unique<FakeLayerHost>(2, mu_core::LayerAccess::TryLock, true);
                auto& p = *pBox;
                p.setUseLayerRenderForTest(true);
                Driver d(p);
                p.mixerEngine.channels[1].sidechainSource.store(MixerEngine::kExtSidechainSrc);
                p.mixerEngine.channels[1].sidechainAmount.store(1.0f);
                p.mixerEngine.channels[1].sidechainReleaseMs.store(20.0f);
                if (withSidechain)
                    d.fillInput = [](juce::AudioBuffer<float>& b, juce::int64 frame)
                    {
                        for (int i = 0; i < b.getNumSamples(); ++i)
                            if ((frame + i) % 700 == 0)
                                for (int c = 0; c < 2; ++c) b.setSample(c, i, 1.0f);
                    };
                juce::AudioBuffer<float> out(2, total);
                int at = 0;
                for (size_t k = 0; at < total; ++k)
                {
                    const int n = juce::jmin(sched[k % sched.size()], total - at);
                    const auto& b = d.block(n);
                    for (int c = 0; c < 2; ++c) out.copyFrom(c, at, b, c, 0, n);
                    at += n;
                }
                return out;
            };
            const auto reference = renderDucked({ 512 }, true);
            const auto unducked = renderDucked({ 512 }, false);
            expect(! bitEqual(reference, unducked, total), "the sidechain ducks channel 1");
            for (const auto& sched : std::vector<std::vector<int>> { { 1500 }, { 4096 }, { 1, 7, 63, 64, 129, 512, 1024, 3 } })
            {
                int diff = -1;
                const bool same = bitEqual(renderDucked(sched, true), reference, total, &diff);
                expect(same,
                       "schedule starting " + juce::String(sched[0]) + " ducks differently from frame " + juce::String(diff));
            }
        }

        beginTest("R1: a host that reports playing with a frozen position is one jump on resume, not one per block");
        {
            auto pBox = std::make_unique<FakeLayerHost>(1); auto& p = *pBox;
            p.setUseLayerRenderForTest(true);
            Driver d(p);
            for (int i = 0; i < 4; ++i) d.block(256);
            d.freezePosition = true;
            for (int i = 0; i < 10; ++i) d.block(256);
            expectEquals(p.fake(0).jumped, 0, "the frozen blocks are not jumps");
            d.freezePosition = false;
            for (int i = 0; i < 4; ++i) d.block(256);
            expectEquals(p.fake(0).jumped, 1, "one jump when the position moves again");
            expectEquals(p.fake(0).jumpHooks, 1);
        }

        beginTest("N3: a skipped block clears the layers' MIDI out, so a merge never re-sends the last rendered block's events");
        {
            auto pBox = std::make_unique<FakeLayerHost>(1); auto& p = *pBox;
            p.setUseLayerRenderForTest(true);
            Driver d(p);
            p.fake(0).emitNoteOn = true;
            d.block(256);
            juce::MidiBuffer first;
            first.ensureSize(256);
            expectEquals(p.mergeLayerMidiOut(first, 200), 1, "the rendered block's note-on");
            {
                struct H { juce::CriticalSection& l; juce::WaitableEvent in, out; std::thread th;
                           explicit H(juce::CriticalSection& c) : l(c), th([this] { l.enter(); in.signal(); out.wait(); l.exit(); }) { in.wait(); }
                           ~H() { out.signal(); th.join(); } } h(p.layersLock);
                d.block(256);   // skipped: a missed try-lock
            }
            juce::MidiBuffer second;
            second.ensureSize(256);
            expectEquals(p.mergeLayerMidiOut(second, 200), 0, "nothing after a skipped block");
            d.block(0);     // a 0-frame block
            expectEquals(p.mergeLayerMidiOut(second, 200), 0, "nothing after a 0-frame block");
        }

        beginTest("A transport-anchored layer with a beat cursor fires no extra trigger over a 10-block freeze");
        {
            // Fires one trigger per quarter beat it crosses, from a last-evaluated-beat cursor (the allowed state),
            // and re-anchors the cursor on a jump.
            struct CursorLayer : FakeBeatLayer
            {
                void render(const mu_core::RenderContext& ctx, juce::AudioBuffer<float>&, int) override
                {
                    if (! ctx.playing) return;
                    if (! anchored) { cursor = ctx.startBeat; anchored = true; }
                    const double end = ctx.endBeat();
                    while (std::floor(cursor * 4.0) < std::floor(end * 4.0)) { cursor = (std::floor(cursor * 4.0) + 1.0) / 4.0; ++triggers; }
                    cursor = juce::jmax(cursor, end);
                }
                void onTransportJump(const mu_core::RenderContext& ctx) override { cursor = ctx.startBeat; }
                double cursor = 0.0;
                bool anchored = false;
                int triggers = 0;
            };
            auto run = [](bool freeze)
            {
                auto pBox = std::make_unique<FakeLayerHost>(1); auto& p = *pBox;
                {
                    const juce::ScopedLock sl(p.layersLock);
                    p.layers[0] = std::make_unique<CursorLayer>();
                }
                p.setUseLayerRenderForTest(true);
                Driver d(p);
                for (int i = 0; i < 30; ++i) d.block(512);
                if (freeze)
                {
                    d.freezePosition = true;
                    for (int i = 0; i < 10; ++i) d.block(512);
                    d.freezePosition = false;
                }
                for (int i = 0; i < 30; ++i) d.block(512);
                return static_cast<CursorLayer&>(*p.layers[0]).triggers;
            };
            const int moving = run(false), frozen = run(true);
            expect(moving > 0);
            expectEquals(frozen, moving, "the frozen blocks repeat startBeat and fire nothing new");
        }

        beginTest("An oversize host block through MixerEngine: every chunk mixed, the tail of the block not lost");
        {
            auto pBox = std::make_unique<FakeLayerHost>(2); auto& p = *pBox;
            AppendTap tap(4096);
            const auto out = renderThrough(p, { 3000 }, 3000, 0.0, &tap);
            expectEquals(tap.written[0], 3000, "channel 0 got all 3000 frames, in chunks");
            expect(tap.maxCall <= 512);
            expect(out.getMagnitude(0, 1024, 476) > 0.0f, "the frames past the prepared size are rendered, not truncated");
        }

        beginTest("Zero allocations and frees inside processBlock, including first block, stop, start, locate, miss");
        {
            auto pBox = std::make_unique<FakeLayerHost>(4); auto& p = *pBox;
            p.setUseLayerRenderForTest(true);
            Driver d(p);
            d.countAllocations = true;
            for (int i = 0; i < 20; ++i) d.block(i % 3 == 0 ? 1500 : 256);
            d.host.playing = false;  d.block(256);  d.block(256);
            d.host.playing = true;   d.block(256);
            d.host.ppq += 13.0;      d.block(256);
            p.layerEditInFlight.store(true);  d.block(256);  p.layerEditInFlight.store(false);
            d.block(0);
            d.block(256);
            expectEquals(d.allocations, 0);
            expectEquals(d.frees, 0);
        }

        beginTest("Priority order and peers (C10): a priority-1 reader gets its source post-insert, pre-strip");
        {
            auto pBox = std::make_unique<FakeLayerHost>(3); auto& p = *pBox;
            p.layers[1] = std::make_unique<PeerReaderLayer>(0);   // reads slot 0
            p.layers[2] = std::make_unique<PeerReaderLayer>(5);   // slot 5 is not live: silence
            AppendTap tap(4096);
            renderThrough(p, { 256 }, 2048, 0.0, &tap);
            int diff = -1;
            bool half = true;
            for (int c = 0; c < 2 && half; ++c)
                for (int i = 0; i < 2048; ++i)
                    if (tap.bufs[1].getSample(c, i) != 0.5f * tap.bufs[0].getSample(c, i)) { half = false; diff = i; break; }
            expect(half, "the reader's channel is exactly half its source, first difference " + juce::String(diff));
            expect(tap.bufs[0].getMagnitude(0, 0, 2048) > 0.0f);
            expect(static_cast<PeerReaderLayer&>(*p.layers[2]).peerMissing > 0, "a non-rendered source reads as absent");
            expectEquals(tap.bufs[2].getMagnitude(0, 0, 2048), 0.0f);
        }

        beginTest("A priority-1 layer is not a peer, and an Empty layer is never called (its channel is silent)");
        {
            auto pBox = std::make_unique<FakeLayerHost>(3); auto& p = *pBox;
            p.layers[0] = std::make_unique<PeerReaderLayer>(1);   // reads another priority-1 layer
            p.layers[1] = std::make_unique<PeerReaderLayer>(2);
            p.layers[2] = std::make_unique<Layer>();               // the base Layer is Empty
            AppendTap tap(1024);
            renderThrough(p, { 256 }, 1024, 0.0, &tap);
            expect(static_cast<PeerReaderLayer&>(*p.layers[0]).peerMissing > 0);
            expectEquals(tap.bufs[2].getMagnitude(0, 0, 1024), 0.0f);
            expectEquals(p.getLayerRenderProbe().mixCount, 3, "the Empty slot is still a mixer channel (cleared)");
        }

        beginTest("A missed lock renders silence but the transport and the next block stay continuous");
        {
            auto pBox = std::make_unique<FakeLayerHost>(2); auto& p = *pBox;
            auto refBox = std::make_unique<FakeLayerHost>(2); auto& ref = *refBox;
            const auto reference = renderThrough(ref, { 256 }, 256 * 20);
            p.setUseLayerRenderForTest(true);
            Driver d(p);
            juce::AudioBuffer<float> out(2, 256 * 20);
            for (int k = 0; k < 20; ++k)
            {
                if (k == 7)
                {
                    Holder h([&] { p.layersLock.enter(); }, [&] { p.layersLock.exit(); });
                    const auto& b = d.block(256);
                    expectEquals(peakOf(b, 256), 0.0f, "a missed lock is a silent block");
                    for (int c = 0; c < 2; ++c) out.copyFrom(c, k * 256, b, c, 0, 256);
                    continue;
                }
                const auto& b = d.block(256);
                for (int c = 0; c < 2; ++c) out.copyFrom(c, k * 256, b, c, 0, 256);
            }
            // Every rendered block matches the reference at its own absolute frames (the beat advanced).
            juce::AudioBuffer<float> a(2, 256), r(2, 256);
            bool same = true;
            for (int k = 8; k < 20; ++k)
            {
                for (int c = 0; c < 2; ++c) { a.copyFrom(c, 0, out, c, k * 256, 256); r.copyFrom(c, 0, reference, c, k * 256, 256); }
                same &= bitEqual(a, r, 256);
            }
            expect(same, "after the miss the layers resume at the transport's position");
            expectEquals(p.fake(0).jumped, 0, "no transportJumped after a missed lock in continuous play (R5)");
            expectEquals(p.getLayerRenderProbe().blocksSkipped, 1);
        }

        beginTest("Edges (C4): justStarted / justStopped once per edge, hooks before render, locate once, never on contiguous blocks");
        {
            auto pBox = std::make_unique<FakeLayerHost>(2); auto& p = *pBox;
            p.setUseLayerRenderForTest(true);
            Driver d(p);
            d.host.playing = false;
            d.block(256);
            d.host.playing = true;
            for (int i = 0; i < 5; ++i) d.block(256);
            expectEquals(p.fake(0).justStarted, 1);
            d.host.ppq = 100.0;            // a locate while playing
            for (int i = 0; i < 3; ++i) d.block(256);
            expectEquals(p.fake(0).jumped, 1);
            expectEquals(p.fake(0).jumpHooks, 1);
            expectEquals(p.fake(1).jumpHooks, 1, "every live layer, slot order");
            expectEquals(p.fake(2).jumpHooks, 0, "a spare above the count is not called");
            d.host.playing = false;
            d.block(256); d.block(256);
            expectEquals(p.fake(0).justStopped, 1);
            expectEquals(p.fake(0).stopHooks, 1);
            d.host.playing = true;
            d.host.ppq = 4.0;              // a locate while stopped is reported by justStarted alone
            d.block(256);
            expectEquals(p.fake(0).justStarted, 2);
            expectEquals(p.fake(0).jumped, 1);
        }

        beginTest("R5: a locate inside an unrendered block is delivered once; the three stop sequences");
        {
            auto pBox = std::make_unique<FakeLayerHost>(1); auto& p = *pBox;
            p.setUseLayerRenderForTest(true);
            Driver d(p);
            for (int i = 0; i < 3; ++i) d.block(256);
            // Locate inside an edit block (unrendered), then continuous play.
            d.host.ppq += 32.0;
            p.layerEditInFlight.store(true);  d.block(256);  p.layerEditInFlight.store(false);
            for (int i = 0; i < 3; ++i) d.block(256);
            expectEquals(p.fake(0).jumped, 1, "the hidden locate reaches the layer once");
            expectEquals(p.fake(0).hiddenStops, 0, "a locate is not a stop");

            // (a) stop in an unrendered block, restart on the next rendered one: one jump, no stop edge.
            const int j0 = p.fake(0).jumped, s0 = p.fake(0).justStopped;
            d.host.playing = false;
            p.layerEditInFlight.store(true);  d.block(256);  p.layerEditInFlight.store(false);
            d.host.playing = true;
            d.block(256); d.block(256);
            expectEquals(p.fake(0).jumped - j0, 1);
            expectEquals(p.fake(0).justStopped - s0, 0);
            expectEquals(p.fake(0).hiddenStops, 1, "the jump says a stop was hidden in it (stoppedUnrendered)");

            // (b) stop in an unrendered block, the next rendered block stopped: justStopped, and no jump later.
            const int j1 = p.fake(0).jumped, s1 = p.fake(0).justStopped;
            d.host.playing = false;
            p.layerEditInFlight.store(true);  d.block(256);  p.layerEditInFlight.store(false);
            d.block(256);
            d.host.playing = true;
            d.block(256); d.block(256);
            expectEquals(p.fake(0).justStopped - s1, 1);
            expectEquals(p.fake(0).jumped - j1, 0);
            expectEquals(p.fake(0).hiddenStops, 1, "a stop the next rendered block sees is justStopped only");

            // (c) pause and resume at the same position inside unrendered blocks: one jump.
            const int j2 = p.fake(0).jumped;
            p.layerEditInFlight.store(true);
            d.host.playing = false;  d.block(256);
            d.host.playing = true;   d.block(256);
            p.layerEditInFlight.store(false);
            d.block(256); d.block(256);
            expectEquals(p.fake(0).jumped - j2, 1);
        }

        beginTest("A 0-frame block renders nothing and calls no hook; its edge reaches the next block");
        {
            auto pBox = std::make_unique<FakeLayerHost>(1); auto& p = *pBox;
            p.setUseLayerRenderForTest(true);
            Driver d(p);
            d.block(256);
            const int renders = p.fake(0).renders;
            d.host.playing = false;
            d.block(0);
            expectEquals(p.fake(0).renders, renders);
            expectEquals(p.fake(0).stopHooks, 0);
            d.block(256);
            expectEquals(p.fake(0).stopHooks, 1);
            expectEquals(p.fake(0).justStopped, 1);
            expectEquals(p.fake(0).hiddenStops, 0);

            // A 0-frame block that sees the stop, then play again: one jump that carries the stop.
            d.host.playing = true;
            d.block(256);
            d.host.playing = false;
            d.block(0);
            d.host.playing = true;
            d.block(256); d.block(256);
            expectEquals(p.fake(0).hiddenStops, 1, "the 0-frame stop reaches the next rendered block as stoppedUnrendered, once");
        }

        beginTest("renderPriority is read once per call: a layer whose priority flips between the passes renders exactly once");
        {
            auto pBox = std::make_unique<FakeLayerHost>(2); auto& p = *pBox;
            p.fake(1).flipPriority = true;
            p.setUseLayerRenderForTest(true);
            Driver d(p);
            for (int i = 0; i < 8; ++i) d.block(256);
            expectEquals(p.fake(1).renders, 8, "one render per block, never zero or two");
            expectEquals(p.fake(0).renders, 8);
        }

        beginTest("beatWrap 64: the host's wrap and deltas of +-1e-4 (and +-64) are not jumps; a real locate is");
        {
            auto pBox = std::make_unique<FakeLayerHost>(1); auto& p = *pBox;
            p.beatWrap = 64.0;
            p.setUseLayerRenderForTest(true);
            Driver d(p);
            d.host.ppq = 63.9;
            for (int i = 0; i < 40; ++i) d.block(256);   // crosses the 64-beat wrap
            expectEquals(p.fake(0).jumped, 0, "the wrap of a bounded host is not a jump");
            for (double nudge : { 1.0e-4, -1.0e-4, 64.0 + 1.0e-4, 64.0 - 1.0e-4, 64.0, 128.0 })
            {
                d.host.ppq += nudge;
                d.block(256);
            }
            expectEquals(p.fake(0).jumped, 0);
            d.host.ppq += 3.0;
            d.block(256);
            expectEquals(p.fake(0).jumped, 1);
        }

        beginTest("R6: a layer started at beat -4 renders its unwrapped reference; with beatWrap 64 it never sees a negative beat");
        {
            constexpr int total = 120000;   // 5 beats at 120 bpm: from beat -4 across beat 0
            auto unboundedBox = std::make_unique<FakeLayerHost>(1); auto& unbounded = *unboundedBox;
            AppendTap tap(total);
            renderThrough(unbounded, { 333 }, total, -4.0, &tap);
            auto referenceBox = std::make_unique<FakeLayerHost>(1); auto& reference = *referenceBox;
            AppendTap refTap(total);
            renderThrough(reference, { 512 }, total, -4.0, &refTap);
            expect(bitEqual(tap.bufs[0], refTap.bufs[0], total), "negative start beats are block-size invariant");
            expect(unbounded.fake(0).lastStartBeat > 0.0, "the transport crossed beat 0");

            auto boundedBox = std::make_unique<FakeLayerHost>(1); auto& bounded = *boundedBox;
            bounded.beatWrap = 64.0;
            renderThrough(bounded, { 256 }, 24000, -4.0);
            expectEquals(bounded.fake(0).negativeInBoundedHost, 0);
            expect(bounded.fake(0).lastStartBeat >= 0.0 && bounded.fake(0).lastStartBeat < 64.0);
        }

        beginTest("Block-size invariance under a tempo ramp and at startBeat 1e6");
        {
            auto aBox = std::make_unique<FakeLayerHost>(2); auto& a = *aBox;
            auto bBox = std::make_unique<FakeLayerHost>(2); auto& b = *bBox;
            const auto ra = renderThrough(a, { 512 }, 12000, 1.0e6);
            const auto rb = renderThrough(b, { 97 }, 12000, 1.0e6);
            expect(bitEqual(ra, rb, 12000), "positions at beat 1e6 stay within one frame");
            // A ramp: the layer's step positions follow the beat (no double fire, no missed step).
            auto rBox = std::make_unique<FakeLayerHost>(1); auto& r = *rBox;
            r.setUseLayerRenderForTest(true);
            Driver d(r);
            for (int i = 0; i < 60; ++i) { d.host.bpm = 90.0 + i; d.block(256); }
            expectEquals(r.fake(0).jumped, 0, "a tempo ramp is continuous, not a jump");
        }

        beginTest("Count policy: RenderPastCount mixes up to the highest active tail; CutAtCount only the live count");
        {
            auto pBox = std::make_unique<FakeLayerHost>(2); auto& p = *pBox;
            p.setUseLayerRenderForTest(true);
            Driver d(p);
            struct Ringing : FakeBeatLayer
            {
                mu_core::RetirePolicy retirePolicy() const override { return { mu_core::RetirePolicy::Kind::Natural, 25.0f, 1.0f }; }
                bool renderRetiring(const mu_core::RenderContext&, juce::AudioBuffer<float>& out, int n, mu_core::RetireState&) override
                { for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::add(out.getWritePointer(c), 0.125f, n); ++tailRenders; return true; }
                int tailRenders = 0;
            };
            auto ring = std::make_unique<Ringing>();
            auto* ringPtr = ring.get();
            p.retireBin.park(5, std::move(ring), p.layoutSerial(), false);
            d.block(256);
            expectEquals(p.getLayerRenderProbe().mixCount, 6);
            expect(ringPtr->tailRenders > 0);

            auto qBox = std::make_unique<FakeLayerHost>(2); auto& q = *qBox;
            q.setRetireCountPolicy(mu_core::RetireBin::CountPolicy::CutAtCount);
            q.setUseLayerRenderForTest(true);
            Driver dq(q);
            auto ring2 = std::make_unique<Ringing>();
            auto* ring2Ptr = ring2.get();
            q.retireBin.park(5, std::move(ring2), q.layoutSerial(), false);
            dq.block(256);
            expectEquals(q.getLayerRenderProbe().mixCount, 2);
            expectEquals(ring2Ptr->tailRenders, 0, "under CutAtCount a tail above the count never renders");
        }

        beginTest("loopBeats is cached per slot and drives the slot's swap boundary (also on a skipped block)");
        {
            auto pBox = std::make_unique<FakeLayerHost>(1); auto& p = *pBox;
            int committed = 0;
            p.layerSwapper.setAppliers([](juce::ValueTree&) {}, [&](int, juce::ValueTree&) { ++committed; });
            p.fake(0).loopBeatsValue = 1.0;   // a one-beat loop, not the 4/4 bar
            p.setUseLayerRenderForTest(true);
            Driver d(p);
            d.host.ppq = 0.25;
            d.block(512);                      // caches loopBeats 1.0
            p.layerSwapper.stageSlot(0, juce::ValueTree("Staged"));
            for (int i = 0; i < 100 && committed == 0; ++i)   // 512 frames = 1/48 beat at 120 bpm
            {
                d.block(512);
                p.flushPendingAsyncUpdates();
            }
            expectEquals(committed, 1, "committed at the one-beat boundary");
            expect(d.host.ppq > 1.0 && d.host.ppq < 1.1, "at beat 1, well before the bar line");
        }

        beginTest("layerLoopBeats: a parked spare caches its own loop length, an empty slot the bar");
        {
            auto pBox = std::make_unique<FakeLayerHost>(1); auto& p = *pBox;
            p.fake(0).loopBeatsValue = 8.0;
            p.fake(3).loopBeatsValue = 12.0;   // a spare above the live count
            {
                const juce::ScopedLock sl(p.layersLock);
                p.layers[5].reset();             // a slot with no layer at all
            }
            p.setUseLayerRenderForTest(true);
            Driver d(p);
            d.block(512);
            expectEquals(p.layerLoopBeats(0), 8.0, "the live layer's length");
            expectEquals(p.layerLoopBeats(3), 12.0, "the parked spare's own length");
            expectEquals(p.layerLoopBeats(5), 4.0, "no layer: the bar");
            expect(p.layerLoopBeats(-1) < 0.0 && p.layerLoopBeats(mu_limits::kMaxLayers) < 0.0, "out of range: no loop");
        }

        beginTest("MIDI: decoded once, each call sees its own frames, overflow is counted, the sinks merge within a budget");
        {
            auto pBox = std::make_unique<FakeLayerHost>(2); auto& p = *pBox;
            p.setUseLayerRenderForTest(true);
            Driver d(p);
            for (int i = 0; i < 5; ++i) d.pendingMidi.push_back(juce::MidiMessage::noteOn(1, 60 + i, (juce::uint8) 100));
            d.midiOffset = 700;    // lands in the second chunk of a 1024 block
            d.block(1024);
            expectEquals(p.fake(0).midiSeen, 5, "every event reaches the layer exactly once across the chunks");
            expectEquals(p.fake(1).midiSeen, 5, "and every layer reads the same decode");
            expectEquals(p.getLayerRenderProbe().midiDropped, 0);

            for (int i = 0; i < mu_limits::kMaxBlockMidiEvents; ++i) d.pendingMidi.push_back(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 1));
            d.midiOffset = 0;
            d.block(256);
            expectEquals(p.getLayerRenderProbe().midiDropped, mu_limits::kMidiNoteOffReserve,
                         "input stops short of the note-off reserve; the rest is counted, not stranded");

            // A live layer emits into its sink; the product merges it with the room it knows it has.
            p.fake(0).emitNoteOn = true;
            d.block(256);
            juce::MidiBuffer outBuf;
            outBuf.ensureSize(256);
            {
                mu_alloc::Armed armed;
                const int added = p.mergeLayerMidiOut(outBuf, 200);
                armed.stop();
                expectEquals(added, 1);
                expectEquals(armed.allocs(), 0, "the merge stays within the caller's room");
            }
            juce::MidiBuffer none;
            expectEquals(p.mergeLayerMidiOut(none, 0), 0, "no budget, nothing added");
        }

        beginTest("Skipped-block MIDI: dropped by default; carried to the next rendered block when the product opts in");
        {
            for (const bool carry : { false, true })
            {
                auto pBox = std::make_unique<FakeLayerHost>(1); auto& p = *pBox;
                p.setCarrySkippedMidi(carry);
                p.setUseLayerRenderForTest(true);
                Driver d(p);
                d.block(256);
                p.layerEditInFlight.store(true);   // an edit silences the next two host blocks
                d.pendingMidi.push_back(juce::MidiMessage::noteOn(1, 64, (juce::uint8) 100));
                d.pendingMidi.push_back(juce::MidiMessage::midiClock());   // a system message is never carried
                d.midiOffset = 100;
                d.block(256);
                d.pendingMidi.push_back(juce::MidiMessage::noteOff(1, 64));
                d.block(256);
                d.block(0);                                  // a 0-frame block renders nothing either
                p.layerEditInFlight.store(false);
                const int seenBefore = p.fake(0).midiSeen;
                d.pendingMidi.push_back(juce::MidiMessage::noteOn(1, 67, (juce::uint8) 100));
                d.midiOffset = 10;
                d.block(256);
                const auto& f = p.fake(0);
                if (carry)
                {
                    expectEquals(f.midiSeen - seenBefore, 3, "the two carried channel messages, then the block's own");
                    expectEquals(f.lastCarried, 2, "the view counts the carried ones");
                    expectEquals(f.firstEventNote, 64, "carried first, in arrival order, at frame 0");
                }
                else
                {
                    expectEquals(f.midiSeen - seenBefore, 1, "only the block's own message (today's behaviour)");
                    expectEquals(f.lastCarried, 0);
                }
                d.block(256);
                expectEquals(p.fake(0).lastCarried, 0, "a carried message is handed over once");
                expectEquals(p.getLayerRenderProbe().midiDropped, 0);
            }
        }

        beginTest("Held keys: a layer created while keys are down is told about them (per channel, once, no allocation)");
        {
            // A fresh layer in slot 0, as setLayerType leaves one: it was not there for the note-ons.
            auto freshLayerIn = [](FakeLayerHost& host, int slot)
            {
                host.layers[(size_t) slot] = std::make_unique<FakeBeatLayer>();
                host.layers[(size_t) slot]->bindParams(host.apvts, slot);
                host.layers[(size_t) slot]->prepare(48000.0, 512);
            };
            auto pBox = std::make_unique<FakeLayerHost>(2); auto& p = *pBox;
            p.setUseLayerRenderForTest(true);
            Driver d(p);
            d.block(256);
            d.pendingMidi.push_back(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100));
            d.pendingMidi.push_back(juce::MidiMessage::noteOn(2, 64, (juce::uint8) 90));
            d.block(256);
            expectEquals(p.fake(0).midiSeen, 2, "the layer that was there heard both presses live");

            freshLayerIn(p, 0);
            d.countAllocations = true;
            d.block(256);
            d.countAllocations = false;
            expectEquals(d.allocations, 0, "seeding the new layer allocates nothing");
            expectEquals(p.fake(0).midiSeen, 2, "Omni: both held keys reach the new layer");
            expectEquals(p.fake(0).lastCarried, 2, "as carried note-ons, not as this block's presses");
            expectEquals(p.fake(0).firstEventNote, 60);
            d.block(256);
            expectEquals(p.fake(0).midiSeen, 2, "once: the next block repeats nothing");
            expectEquals(p.fake(0).lastCarried, 0);

            // A release before the layer exists leaves nothing to seed; a key pressed in the same block is not doubled.
            d.pendingMidi.push_back(juce::MidiMessage::noteOff(1, 60));
            d.block(256);
            freshLayerIn(p, 1);
            d.pendingMidi.push_back(juce::MidiMessage::noteOn(3, 72, (juce::uint8) 100));
            d.block(256);
            expectEquals(p.fake(1).midiSeen, 2, "the key still down (64) plus the block's own press (72); 60 was released");
            expectEquals(p.fake(1).lastCarried, 1);

            // A per-layer MIDI In: only that channel's keys are seeded, and a change of channel seeds the new one.
            std::atomic<float> midiIn { 2.0f };
            p.midiInRaw[0] = &midiIn;
            freshLayerIn(p, 0);
            const int before = p.fake(0).midiSeen;
            d.block(256);
            expectEquals(p.fake(0).midiSeen - before, 1, "channel 2 gets its own key only");
            expectEquals(p.fake(0).firstEventNote, 64);
            midiIn.store(3.0f);
            d.block(256);
            expectEquals(p.fake(0).lastCarried, 1, "moved to channel 3: the key down there (72)");
            expectEquals(p.fake(0).firstEventNote, 72);
            midiIn.store(5.0f);
            const int beforeEmpty = p.fake(0).midiSeen;
            d.block(256);
            expectEquals(p.fake(0).midiSeen - beforeEmpty, 0, "a channel with no key down gets nothing");
            p.midiInRaw[0] = nullptr;
        }

        beginTest("Held keys: presses and releases in a block nothing renders still count");
        {
            auto pBox = std::make_unique<FakeLayerHost>(2); auto& p = *pBox;
            p.setUseLayerRenderForTest(true);
            Driver d(p);
            d.block(256);
            p.layerEditInFlight.store(true);
            d.pendingMidi.push_back(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100));
            d.pendingMidi.push_back(juce::MidiMessage::noteOn(1, 62, (juce::uint8) 100));
            d.block(256);
            d.pendingMidi.push_back(juce::MidiMessage::noteOff(1, 60));
            d.block(256);
            p.layerEditInFlight.store(false);
            p.layers[0] = std::make_unique<FakeBeatLayer>();
            p.layers[0]->bindParams(p.apvts, 0);
            d.block(256);
            expectEquals(p.fake(0).midiSeen, 1, "62 is held, 60 was let go in the skipped block");
            expectEquals(p.fake(0).firstEventNote, 62);
        }

        beginTest("HeldKeyTable: velocity per channel and note, note-off and velocity-0 release, channel clears, capacity");
        {
            mu_core::HeldKeyTable t;
            auto ev = [](uint8_t b0, uint8_t b1, uint8_t b2) { mu_core::MidiEvent e {}; e.size = 3; e.bytes[0] = b0; e.bytes[1] = b1; e.bytes[2] = b2; return e; };
            expect(! t.anyHeld());
            t.apply(ev(0x90, 60, 100)); t.apply(ev(0x91, 60, 50)); t.apply(ev(0x91, 61, 70)); t.apply(ev(0x90, 60, 90));
            expectEquals(t.numHeld(), 3, "a repeated note-on does not count twice");
            mu_core::MidiEvent out[8] {};
            expectEquals(t.collect(out, 8, 7, 0xFFFF), 3);
            expectEquals((int) out[0].bytes[2], 90, "the latest velocity");
            expectEquals((int) out[0].frame, 7);
            expectEquals((int) (out[1].bytes[0] & 0x0f), 1);
            expectEquals(t.collect(out, 8, 0, 0x0001), 1, "mask: channel 1 only");
            expectEquals(t.collect(out, 2, 0, 0xFFFF), 2, "never past the capacity");
            t.apply(ev(0x80, 60, 0)); t.apply(ev(0x91, 61, 0));
            expectEquals(t.numHeld(), 1, "note-off and note-on at velocity 0 release (channel 2's 60 is left)");
            t.apply(ev(0x90, 40, 100));
            t.apply(ev(0xb1, 123, 0));
            expectEquals(t.numHeld(), 1, "All Notes Off clears its channel only (channel 1's 40 is left)");
            t.apply(ev(0xf8, 0, 0));
            t.clear();
            expect(! t.anyHeld());
        }

        beginTest("A parked spare above the live count follows the block's MIDI and renders nothing; live layers are never parked");
        {
            auto pBox = std::make_unique<FakeLayerHost>(2); auto& p = *pBox;
            p.setUseLayerRenderForTest(true);
            Driver d(p);
            d.pendingMidi.push_back(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100));
            d.block(1024);   // two chunks: one parked call per chunk, the event in the first
            expectEquals(p.fake(0).parkedCalls, 0);
            expectEquals(p.fake(1).parkedCalls, 0);
            for (int s = 2; s < mu_limits::kMaxLayers; ++s)
            {
                expectEquals(p.fake(s).parkedCalls, 2);
                expectEquals(p.fake(s).parkedSlot, s);
                expectEquals(p.fake(s).parkedMidiSeen, 1);
                expectEquals(p.fake(s).renders, 0, "a parked layer is never rendered");
            }
            expectEquals(p.getLayerRenderProbe().mixCount, 2, "and never mixed");
            p.layerEditInFlight.store(true);
            d.block(256);
            p.layerEditInFlight.store(false);
            expectEquals(p.fake(2).parkedCalls, 2, "a skipped block calls no parked layer");
        }

        beginTest("C11: denormals are flushed inside every layer render even without the product's own guard");
        {
            struct DenormalProbe : FakeBeatLayer
            {
                void render(const mu_core::RenderContext& ctx, juce::AudioBuffer<float>& out, int n) override
                {
                    flushed = flushed && juce::FloatVectorOperations::areDenormalsDisabled();
                    FakeBeatLayer::render(ctx, out, n);
                }
                bool flushed = true;
            };
            auto pBox = std::make_unique<FakeLayerHost>(1); auto& p = *pBox;
            p.layers[0] = std::make_unique<DenormalProbe>();
            p.setUseLayerRenderForTest(true);
            Driver d(p);
            mu_core::BlockTransport t;
            t.playing = true; t.bpm = 120.0; t.beatsPerSample = 2.0 / 48000.0; t.blockBeats = t.beatsPerSample * 64;
            juce::AudioBuffer<float> buf(2, 64);
            juce::MidiBuffer m;
            p.renderAndMixForTest(buf, m, t);   // no ScopedNoDenormals of the caller's
           #if JUCE_INTEL
            expect(static_cast<DenormalProbe&>(*p.layers[0]).flushed);
           #endif
        }
    }
};
static LayerRenderDriverTests sLayerRenderDriverTests;

// ─── Structural edits: rebinding (C9) and the packed layout word (R4, risk R16) ──────────────────────
struct LayerLayoutWordTests : juce::UnitTest
{
    LayerLayoutWordTests() : juce::UnitTest("Layout word, onMoved rebinding and the applyComposedState race (C9, R4, R16)", "mu-core") {}

    void runTest() override
    {
        beginTest("Every count store advances the serial; numLayers mirrors the count");
        {
            auto pBox = std::make_unique<FakeLayerHost>(2); auto& p = *pBox;
            const auto s0 = p.layoutSerial();
            expectEquals(p.addLayer(), 2);
            expectEquals(p.getNumLayers(), 3);
            expect(p.layoutSerial() == s0 + 1);
            expect(p.removeLayer(0));
            expect(p.layoutSerial() == s0 + 2);
            expect(p.swapLayers(0, 1));
            expect(p.layoutSerial() == s0 + 3, "a swap moves to the next serial too");
            const auto state = p.captureComposedState();
            p.applyComposedState(state);
            expect(p.layoutSerial() == s0 + 4);
            expectEquals((int) mu_core::LayoutWord::count(p.layerAccess.layout.load()), p.getNumLayers());
        }

        beginTest("C9: moved layers are re-bound to their new slot; an added layer is bound before it is published");
        {
            auto pBox = std::make_unique<FakeLayerHost>(3); auto& p = *pBox;
            auto* a = &p.fake(0);
            auto* b = &p.fake(2);
            expect(p.swapLayers(0, 2));
            expectEquals(a->boundSlot, 2);
            expectEquals(b->boundSlot, 0);
            auto* moved = &p.fake(1);
            expect(p.removeLayer(0));
            expectEquals(moved->boundSlot, 0, "a layer shifted down by a remove is re-bound");
            const int idx = p.addLayer();
            expectEquals(p.fake(idx).boundSlot, idx);
            // The binding reads the new slot's parameter.
            p.apvts.getParameter("v0_a")->setValueNotifyingHost(1.0f);
            expect(std::abs(moved->gainParam.load() - 1.0f) < 1.0e-6f);
        }

        beginTest("R16: applyComposedState on a host thread against message-thread parks loses or misattaches no serial");
        {
            constexpr int kHostApplies = 300, kMessageStores = 3000;
            auto pBox = std::make_unique<FakeLayerHost>(2); auto& p = *pBox;
            const auto state = p.captureComposedState();
            const auto s0 = p.layoutSerial();
            std::atomic<int> mismatches { 0 };
            std::thread host([&] { for (int i = 0; i < kHostApplies; ++i) p.applyComposedState(state); });
            for (int i = 0; i < kMessageStores; ++i)
            {
                // A park outside layersLock: nextLayoutSerial() and the store that carries it inside one hold.
                const juce::ScopedLock lw(p.layoutWriteLock);
                const auto expected = p.nextLayoutSerial();
                const auto stored = p.storeLayerCount(2);
                if (stored != expected) mismatches.fetch_add(1);
            }
            host.join();
            expectEquals(mismatches.load(), 0, "the park's serial is the serial its count store carries");
            expectEquals((int) (p.layoutSerial() - s0), kHostApplies + kMessageStores, "no serial is lost");
        }

        beginTest("TryLock two-thread stress: structural edits and tail parks against a rendering thread");
        {
            constexpr int kBlocks = 4000, kEdits = 400;
            static std::atomic<int> tailUseAfterFree { 0 };
            auto pBox = std::make_unique<FakeLayerHost>(2); auto& p = *pBox;
            p.setUseLayerRenderForTest(true);
            Driver d(p);   // prepares on this thread
            std::atomic<int> nonFinite { 0 }, audioAllocs { 0 };
            std::thread audio([&]
            {
                juce::AudioBuffer<float> buf(2, 256);
                juce::MidiBuffer m;
                mu_core::BlockTransport t;
                t.playing = true; t.bpm = 120.0; t.beatsPerSample = 2.0 / 48000.0; t.blockBeats = t.beatsPerSample * 256;
                for (int i = 0; i < kBlocks; ++i)
                {
                    buf.clear();
                    mu_alloc::Armed armed;
                    p.renderAndMixForTest(buf, m, t);
                    armed.stop();
                    audioAllocs.fetch_add(armed.allocs() + armed.frees());
                    for (int c = 0; c < 2; ++c)
                        for (int k = 0; k < 256; ++k)
                            if (! std::isfinite(buf.getSample(c, k))) { nonFinite.fetch_add(1); break; }
                    t.startBeat += t.blockBeats;
                }
            });
            struct Tail : FakeBeatLayer
            {
                mu_core::RetirePolicy retirePolicy() const override { return { mu_core::RetirePolicy::Kind::Fade, 5.0f, 1.0f }; }
                bool renderRetiring(const mu_core::RenderContext&, juce::AudioBuffer<float>& out, int n, mu_core::RetireState& rs) override
                {
                    if (magic != kAliveMagic) ++tailUseAfterFree;
                    for (int ch = 0; ch < 2; ++ch) juce::FloatVectorOperations::add(out.getWritePointer(ch), 0.01f, n);
                    mu_core::applyLinearFade(out, n, rs);
                    return true;
                }
            };
            for (int e = 0; e < kEdits; ++e)
            {
                switch (e % 4)
                {
                    case 0: p.addLayer(); break;
                    case 1: if (p.getNumLayers() > 1) p.removeLayer(0); break;
                    case 2: if (p.getNumLayers() > 1) p.swapLayers(0, p.getNumLayers() - 1); break;
                    default:
                    {
                        const juce::ScopedLock lw(p.layoutWriteLock);
                        const juce::ScopedLock sl(p.layersLock);
                        p.retireBin.park(e % mu_limits::kMaxLayers, std::make_unique<Tail>(), p.layoutSerial(), true);
                        break;
                    }
                }
                p.flushPendingAsyncUpdates();
                if (p.getNumLayers() >= mu_limits::kMaxLayers) p.removeLayer(1);
            }
            audio.join();
            expectEquals(nonFinite.load(), 0);
            expectEquals(audioAllocs.load(), 0, "the audio thread allocated and freed nothing");
            p.retireBin.cutAll();
            p.retireBin.service();
            expectEquals(p.retireBin.countIn(mu_core::RetireBin::Done) + p.retireBin.countIn(mu_core::RetireBin::Draining)
                         + p.retireBin.countIn(mu_core::RetireBin::Parked), 0, "deterministic end state: the bin is empty");
            for (int s = 0; s < mu_limits::kMaxLayers; ++s) expectEquals(p.fake(s).useAfterFree, 0);
            expectEquals(tailUseAfterFree.load(), 0, "no tail was rendered after it was freed");
        }
    }
};
static LayerLayoutWordTests sLayerLayoutWordTests;

// ─── Small contract pieces ───────────────────────────────────────────────────────────────────────────
struct LayerContractMiscTests : juce::UnitTest
{
    LayerContractMiscTests() : juce::UnitTest("Layer contract: limits, type ids, defaults, the test switch", "mu-core") {}

    void runTest() override
    {
        beginTest("Limits: per-target capacity, the bus and paging constants, the bin and MIDI storage");
        logMessage("sizeof: ProcessorBase " + juce::String((int) sizeof (ProcessorBase)) + ", RetireBin "
                   + juce::String((int) sizeof (mu_core::RetireBin)) + ", LayerAccessState "
                   + juce::String((int) sizeof (mu_core::LayerAccessState)) + ", MixerEngine " + juce::String((int) sizeof (MixerEngine))
                   + ", Layer " + juce::String((int) sizeof (Layer))
                   + " (information)");
        expectEquals(mu_limits::kMaxLayers, MU_MAX_LAYERS);
        expectEquals(mu_limits::kMaxBuses, MU_MAX_BUSES);
        expectEquals(mu_limits::kBusInserts, MU_BUS_INSERTS);
        expectEquals(mu_limits::kLayersPerPage, 8);
        expectEquals(mu_limits::kRetireDepth, 4);
        expectEquals(MixerEngine::MaxChannels, mu_limits::kMaxLayers);

        beginTest("LayerTypeId is saved by name and round-trips; an unknown name is Empty");
        for (int i = 0; i <= (int) mu_core::LayerTypeId::Test2; ++i)
        {
            const auto id = (mu_core::LayerTypeId) i;
            expect(mu_core::layerTypeFromName(mu_core::layerTypeName(id)) == id);
        }
        expect(mu_core::layerTypeFromName("Theremin") == mu_core::LayerTypeId::Empty);
        expect(mu_core::layerTypeFromName(nullptr) == mu_core::LayerTypeId::Empty);

        beginTest("The base Layer's contract defaults do nothing (a product that has not adopted it is unchanged)");
        Layer l;
        expectEquals(l.typeId(), 0);
        expectEquals(l.renderPriority(), 0);
        expectEquals(l.loopBeats(), 4.0);
        expect(l.retirePolicy().kind == mu_core::RetirePolicy::Kind::None);
        Layer copy = l;
        expect(! copy.inRetirePath());

        beginTest("useLayerRender defaults off and the channel tap defaults to none");
        auto pBox = std::make_unique<FakeLayerHost>(1); auto& p = *pBox;
        expect(! p.usesLayerRender());
        expect(p.mixerEngine.channelTap == nullptr);
        p.setUseLayerRenderForTest(true);
        expect(p.usesLayerRender());
    }
};
static LayerContractMiscTests sLayerContractMiscTests;

// The driver's per-block product hooks (layersBlockStarting / layersBlockRendered): once each per rendered host
// block, the start before any layer renders and with the block's view and transport, the end after the mixer; a
// block that renders nothing (a held lock, an edit in flight, 0 frames) calls neither.
namespace
{
    struct HookedHost : FakeLayerHost
    {
        HookedHost() : FakeLayerHost(2) {}
        void layersBlockStarting(const mu_core::LayerView& view, const mu_core::BlockTransport& t, juce::MidiBuffer& midi) override
        {
            ++starts;
            startLive = view.numLive;
            startBeat = t.startBeat;
            rendersAtStart = fake(0).renders;
            midi.addEvent(juce::MidiMessage::noteOn(1, 64, (juce::uint8) 100), 0);   // the host buffer is the one the block got
        }
        void layersBlockRendered(const mu_core::LayerView& view, juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
        {
            ++ends;
            endLive = view.numLive;
            rendersAtEnd = fake(0).renders;
            endFrames = buffer.getNumSamples();
            endPeak = buffer.getMagnitude(0, 0, buffer.getNumSamples());
        }
        int starts = 0, ends = 0, startLive = -1, endLive = -1, rendersAtStart = -1, rendersAtEnd = -1, endFrames = 0;
        double startBeat = -1.0;
        float endPeak = 0.0f;
    };
}

struct LayerBlockHookTests : juce::UnitTest
{
    LayerBlockHookTests() : juce::UnitTest("Render contract: the product's per-block hooks", "mu-core") {}

    void runTest() override
    {
        beginTest("start before any layer renders, end after the mixer, once each per rendered block; none on a skipped block");
        auto pBox = std::make_unique<HookedHost>();
        auto& p = *pBox;
        p.setUseLayerRenderForTest(true);
        Driver d(p, 48000.0, 256);
        d.host.ppq = 2.0;
        const auto& out = d.block(256);
        expectEquals(p.starts, 1);
        expectEquals(p.ends, 1);
        expectEquals(p.startLive, 2, "the start sees the block's view");
        expectEquals(p.startBeat, 2.0, "and its transport");
        expectEquals(p.rendersAtStart, 0, "no layer had rendered at the start");
        expectEquals(p.rendersAtEnd, 1, "every layer had rendered by the end");
        expectEquals(p.endFrames, 256, "the end gets the host buffer the block rendered");
        expect(p.endPeak > 0.0f && p.endPeak == out.getMagnitude(0, 0, 256), "after the mixer wrote it");
        int noteOns = 0;
        for (const auto m : d.midi) noteOns += m.getMessage().isNoteOn() ? 1 : 0;
        expectEquals(noteOns, 1, "the start's MIDI went into the block's host buffer");

        {
            const juce::ScopedLock held(p.layersLock);   // the audio thread's try-lock misses: nothing renders
            std::thread t([&] { d.block(256); });
            t.join();
        }
        d.block(0);
        expectEquals(p.starts, 1, "a skipped block and a 0-frame block call no hook");
        expectEquals(p.ends, 1);
        d.block(128);
        expectEquals(p.starts, 2);
        expectEquals(p.ends, 2);
    }
};
static LayerBlockHookTests sLayerBlockHookTests;
