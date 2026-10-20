// Typed slots (docs/mu-tronic/design.md 2.1.2): the layer type registry and ProcessorBase::setLayerType, proven on fake
// layer types before any product registers a real one. A Published-mode host changes a slot's type in two view
// publications with nothing muted: the other slots' output is bit-identical, a block renders the old layer or the
// new one (never both, never neither), a retiring layer sounds out in the shared bin, and the audio thread keeps its
// rules (no allocation, no overflow views) while the message thread hammers type changes.
//
// Threads: every multi-thread test uses fixed iteration counts and asserts no timing.

#include "AllocationProbe.h"
#include "LayerContractFixtures.h"
#include "ProcessorSeamHelpers.h"
#include "Sequencer/LayerTypeRegistry.h"

#include <atomic>
#include <functional>
#include <thread>

using namespace mu_contract_test;

namespace
{
    using mu_core::LayerTypeId;

    std::atomic<int> gSlot1Arp { 0 }, gSlot1Kick { 0 }, gUseAfterFree { 0 };

    // The Arp test type: the fake beat layer, counting its slot-1 renders.
    struct ArpT : FakeBeatLayer
    {
        void render(const mu_core::RenderContext& ctx, juce::AudioBuffer<float>& out, int n) override
        {
            if (magic != kAliveMagic) gUseAfterFree.fetch_add(1);
            if (ctx.slot == 1) gSlot1Arp.fetch_add(1);
            FakeBeatLayer::render(ctx, out, n);
        }
    };

    // A second type: a constant tone while the transport plays.
    struct KickT : FakeBeatLayer
    {
        int typeId() const override { return (int) LayerTypeId::Kick; }
        void render(const mu_core::RenderContext& ctx, juce::AudioBuffer<float>& out, int n) override
        {
            if (magic != kAliveMagic) gUseAfterFree.fetch_add(1);
            if (ctx.slot == 1) gSlot1Kick.fetch_add(1);
            if (! ctx.playing) return;
            for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::fill(out.getWritePointer(c), 0.3f, n);
        }
    };

    // A third type with a tail: it fades out over 25 ms in the bin after it leaves its slot.
    struct FadingT : KickT
    {
        int typeId() const override { return (int) LayerTypeId::Hat; }
        mu_core::RetirePolicy retirePolicy() const override { return { mu_core::RetirePolicy::Kind::Fade, 25.0f, 2.0f }; }
        bool renderRetiring(const mu_core::RenderContext&, juce::AudioBuffer<float>& out, int n, mu_core::RetireState& rs) override
        {
            for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::add(out.getWritePointer(c), 0.25f, n);
            mu_core::applyLinearFade(out, n, rs);
            return true;
        }
    };

    struct EmptyT : Layer
    {
        EmptyT() { name.clear(); }
        int typeId() const override { return (int) LayerTypeId::Empty; }
    };

    // Set while a create function runs: the hook plays the part of a host thread landing a prepareToPlay between the
    // new layer's build and its publication.
    std::function<void()>* gOnCreate = nullptr;
    template <class T> std::unique_ptr<Layer> makeT(int, LayerTypeId)
    {
        if (gOnCreate != nullptr && *gOnCreate) (*gOnCreate)();
        return std::make_unique<T>();
    }

    // Four live slots (more spares), all ArpT, in Published mode with the test types offered.
    struct TypedHost : FakeLayerHost
    {
        // `prefixLetters`: the types name their parameter prefixes (Hat 'x', the others 'v') and each slot carries both
        // v{N}_ and x{N}_, as a typed slot host does; the x rows hold no parameters.
        explicit TypedHost(int live = 4, bool prefixLetters = false) : FakeLayerHost(live, mu_core::LayerAccess::Published)
        {
            auto offer = [this, prefixLetters](LayerTypeId id, const char* name, mu_core::LayerTypeInfo::CreateFn fn)
            {
                mu_core::LayerTypeInfo i;
                i.id = id; i.name = name; i.group = "Test"; i.create = fn;
                if (prefixLetters) i.paramPrefixLetter = id == LayerTypeId::Hat ? 'x' : 'v';
                layerTypes.add(i);
            };
            if (prefixLetters)
            {
                std::vector<juce::StringArray> sets;
                for (int s = 0; s < mu_limits::kMaxLayers; ++s)
                    sets.push_back(juce::StringArray { "v" + juce::String(s) + "_", "x" + juce::String(s) + "_" });
                initLayerStatePrefixSets(sets);
            }
            offer(LayerTypeId::Arp,  "Arp",  &makeT<ArpT>);
            offer(LayerTypeId::Kick, "Kick", &makeT<KickT>);
            offer(LayerTypeId::Hat,  "Hat",  &makeT<FadingT>);
            for (int i = 0; i < mu_limits::kMaxLayers; ++i)
            {
                layers[(size_t) i] = std::make_unique<ArpT>();
                layers[(size_t) i]->colourIndex = i;
                layers[(size_t) i]->bindParams(apvts, i);
            }
            publishLayers(live);
        }
        bool layersAreSlots() const override { return true; }
        std::unique_ptr<Layer> createLayer(int) override { return std::make_unique<EmptyT>(); }
        using ProcessorBase::layerTypes;
        LayerTypeId typeAt(int s) const { return (LayerTypeId) getLayer(s)->typeId(); }
    };

    struct ScopedTypeFlags
    {
        ScopedTypeFlags() { gSlot1Arp = 0; gSlot1Kick = 0; gUseAfterFree = 0; }
    };

    // Renders `blocks` blocks of `n` frames through the host and returns what each strip handed the mixer.
    struct Run
    {
        explicit Run(TypedHost& host, int totalFrames) : p(host), tap(totalFrames), d(host)
        {
            p.setUseLayerRenderForTest(true);
            p.setChannelTapForTest(&tap);
        }
        ~Run() { p.setChannelTapForTest(nullptr); }
        void block(int n = 256) { d.block(n); }
        TypedHost& p;
        AppendTap tap;
        Driver d;
    };

    bool streamsEqual(const AppendTap& a, const AppendTap& b, int ch, int from, int to)
    {
        for (int c = 0; c < 2; ++c)
            if (std::memcmp(a.bufs[(size_t) ch].getReadPointer(c) + from, b.bufs[(size_t) ch].getReadPointer(c) + from,
                            sizeof(float) * (size_t) (to - from)) != 0)
                return false;
        return true;
    }
}

// ─── The registry ────────────────────────────────────────────────────────────────────────────────────
struct LayerTypeRegistryTests : juce::UnitTest
{
    LayerTypeRegistryTests() : juce::UnitTest("LayerTypeRegistry: the list of types a host offers a slot", "mu-core") {}

    void runTest() override
    {
        beginTest("add, find, groups and refusals");
        mu_core::LayerTypeRegistry r;
        expect(r.isEmpty());
        expect(r.find(LayerTypeId::Arp) == nullptr);

        mu_core::LayerTypeInfo arp;
        arp.id = LayerTypeId::Arp; arp.name = "Arp"; arp.group = "mu-Toni"; arp.create = &makeT<ArpT>;
        mu_core::LayerTypeInfo kick = arp;
        kick.id = LayerTypeId::Kick; kick.name = "Kick"; kick.group = "mu-On";
        mu_core::LayerTypeInfo snare = kick;
        snare.id = LayerTypeId::Snare; snare.name = "Snare";

        expect(r.add(arp));
        expect(r.add(kick));
        expect(r.add(snare));
        expectEquals(r.size(), 3);
        expect(r.find(LayerTypeId::Kick) != nullptr && juce::String(r.find(LayerTypeId::Kick)->name) == "Kick");
        expect(r.find(LayerTypeId::Hat) == nullptr);

        expect(! r.add(arp), "a type is registered once");
        mu_core::LayerTypeInfo empty = arp;
        empty.id = LayerTypeId::Empty;
        expect(! r.add(empty), "Empty is never registered");
        mu_core::LayerTypeInfo noCreate = arp;
        noCreate.id = LayerTypeId::Bass;
        noCreate.create = nullptr;
        expect(! r.add(noCreate), "a type with no create function is refused");
        expectEquals(r.size(), 3);

        const auto groups = r.groups();
        expectEquals(groups.size(), 2);
        expectEquals(groups[0], juce::String("mu-Toni"));
        expectEquals(groups[1], juce::String("mu-On"));
        expectEquals((int) r.inGroup("mu-On").size(), 2);
        expectEquals((int) r.inGroup("mu-Toni").size(), 1);
        expectEquals((int) r.inGroup("mu-Clid").size(), 0);

        beginTest("the type names round trip by name, an unknown name is Empty");
        for (int i = 0; i <= (int) LayerTypeId::Rumble; ++i)
            expect((int) mu_core::layerTypeFromName(mu_core::layerTypeName((LayerTypeId) i)) == i);
        expect(mu_core::layerTypeFromName("Theremin") == LayerTypeId::Empty);
        expect(mu_core::layerTypeFromName(nullptr) == LayerTypeId::Empty);
        expect(mu_core::layerTypeFromName("") == LayerTypeId::Empty);
    }
};
static LayerTypeRegistryTests sLayerTypeRegistryTests;

// ─── setLayerType ────────────────────────────────────────────────────────────────────────────────────
struct LayerTypeSwapTests : juce::UnitTest
{
    LayerTypeSwapTests() : juce::UnitTest("setLayerType: non-muting type change on a Published host (C1)", "mu-core") {}

    void runTest() override
    {
        refusalsAndIdentity();
        publishedViewAndSerial();
        otherSlotsUntouched();
        retireTail();
        parametersAndStrip();
        preparedForTheCurrentBlockSize();
        deleteEmptiesTheSlot();
        listEditsRefused();
        uiLetsGoFirst();
        typedPrefixes();
        followerRestarts();
        resetLetsATailFinish();
        stress();
    }

    // A slot with several prefixes: setLayerType makes the new type's prefix live and resets only that one; a type
    // with another prefix leaves the old rows dormant (untouched, never saved).
    void typedPrefixes()
    {
        beginTest("a type change resets only the new type's prefix; the old type's rows stay dormant and are not saved");
        auto box = std::make_unique<TypedHost>(4, true); auto& p = *box;
        const auto rows = [&](int slot)
        {
            int n = 0;
            const auto node = mu_pp::findLayerNode(p.captureComposedState(), slot);
            for (int i = 0; i < node.getNumChildren(); ++i) n += node.getChild(i).hasType(mu_pp::kRowTag) ? 1 : 0;
            return n;
        };
        expectEquals(rows(1), 0, "no type has been set through setLayerType: no prefix is live");

        mu_seam_test::setParam(p.apvts, "v1_a", 0.9f);
        expect(p.setLayerType(1, LayerTypeId::Kick));   // 'v'
        expectWithinAbsoluteError(mu_seam_test::getParam(p.apvts, "v1_a"), 0.25f, 0.001f);
        expectEquals(rows(1), 1);

        mu_seam_test::setParam(p.apvts, "v1_a", 0.9f);
        expect(p.setLayerType(1, LayerTypeId::Arp));   // the same prefix: reset again
        expectWithinAbsoluteError(mu_seam_test::getParam(p.apvts, "v1_a"), 0.25f, 0.001f);

        mu_seam_test::setParam(p.apvts, "v1_a", 0.9f);
        expect(p.setLayerType(1, LayerTypeId::Hat));   // 'x': v1_ goes dormant
        expectWithinAbsoluteError(mu_seam_test::getParam(p.apvts, "v1_a"), 0.9f, 0.001f);
        expectEquals(rows(1), 0, "the dormant prefix is not written");
        expect(p.setLayerType(1, LayerTypeId::Empty));
        expectEquals(rows(1), 0);
        p.flushPendingAsyncUpdates();
    }

    // C17: a type change replaces neither the strip nor its sidechain source, so the follower is left as it is (zeroing
    // it would only unduck a strip that is still being ducked); a reset returns the strip to defaults, so the audio
    // thread zeroes it at its next block start, never written from the message thread (also for an Empty slot).
    void followerRestarts()
    {
        beginTest("a type change leaves the strip's follower alone; a reset restarts it from zero on the audio thread");
        auto box = std::make_unique<TypedHost>(); auto& p = *box;
        expect(p.setLayerType(2, LayerTypeId::Empty));   // a silent source
        mu_seam_test::setParam(p.apvts, "ch1_scSrc", 3.0f);   // slot 2 (source numbers are 1-based)
        mu_seam_test::setParam(p.apvts, "ch1_scAmt", 1.0f);
        Run run(p, 8 * 256);
        run.block(256);

        p.mixerEngine.setSidechainEnv(1, 0.9f);
        run.block(256);
        expect(p.mixerEngine.getSidechainEnv(1) > 0.5f, "no change, no restart: the follower only decays a little");

        p.mixerEngine.setSidechainEnv(1, 0.9f);
        expect(p.setLayerType(1, LayerTypeId::Kick));
        expectWithinAbsoluteError(p.mixerEngine.getSidechainEnv(1), 0.9f, 1.0e-6f, "setLayerType writes nothing the audio thread owns");
        run.block(256);
        expect(p.mixerEngine.getSidechainEnv(1) > 0.5f, "a type change does not restart the follower: it only decays a little");

        p.mixerEngine.setSidechainEnv(1, 0.9f);
        p.resetLayer(1);
        run.block(256);
        expectEquals(p.mixerEngine.getSidechainEnv(1), 0.0f, "reset restarts it as well");

        expect(p.setLayerType(1, LayerTypeId::Empty));
        run.block(256);
        p.mixerEngine.setSidechainEnv(1, 0.9f);
        p.resetLayer(1);   // an Empty slot: no type change happens, the reset itself asks
        run.block(256);
        expectEquals(p.mixerEngine.getSidechainEnv(1), 0.0f, "reset of an Empty slot restarts it");
        p.flushPendingAsyncUpdates();
    }

    // resetLayer on a Published host publishes twice (Empty, then the type again): a tail the first one parked must
    // still sound out, and be freed.
    void resetLetsATailFinish()
    {
        beginTest("resetLayer on a Published host: the old layer's tail finishes after the two publications");
        constexpr int kBlock = 256;
        auto box = std::make_unique<TypedHost>(); auto& p = *box;
        auto refBox = std::make_unique<TypedHost>(); auto& ref = *refBox;
        expect(p.setLayerType(1, LayerTypeId::Hat));
        expect(ref.setLayerType(1, LayerTypeId::Kick));   // the same tone, no tail
        Run run(p, 40 * kBlock), refRun(ref, 40 * kBlock);
        for (int b = 0; b < 4; ++b) { run.block(kBlock); refRun.block(kBlock); }

        p.resetLayer(1);
        ref.resetLayer(1);
        for (int b = 4; b < 40; ++b) { run.block(kBlock); refRun.block(kBlock); }
        const auto level = [&](const AppendTap& t, int block) { return t.bufs[1].getMagnitude(0, block * kBlock, kBlock); };
        expect(level(run.tap, 4) > level(refRun.tap, 4) + 0.01f, "the first block after the reset carries the tail");
        expect(std::abs(level(run.tap, 39) - level(refRun.tap, 39)) < 1.0e-6f, "the tail has faded out");
        p.flushPendingAsyncUpdates();
        expectEquals(p.retireBin.countIn(mu_core::RetireBin::Parked) + p.retireBin.countIn(mu_core::RetireBin::Draining), 0);
        expectEquals(p.retireBin.reclaimPending(), 0, "the old layer is freed");
    }

    void refusalsAndIdentity()
    {
        beginTest("a TryLock host, a slot out of range and a type the host does not offer are refused; the same type is a no-op");
        {
            auto box = std::make_unique<FakeLayerHost>(2); auto& tryLock = *box;
            expect(! tryLock.setLayerType(0, LayerTypeId::Kick), "TryLock hosts edit through add / remove");
        }
        auto box = std::make_unique<TypedHost>(); auto& p = *box;
        expect(! p.setLayerType(-1, LayerTypeId::Kick));
        expect(! p.setLayerType(mu_limits::kMaxLayers, LayerTypeId::Kick));
        expect(! p.setLayerType(0, LayerTypeId::Rumble), "not offered");
        const auto* before = p.getLayer(0);
        expect(p.setLayerType(0, LayerTypeId::Arp), "the type it has");
        expect(p.getLayer(0) == before, "nothing was rebuilt");

        beginTest("the slot keeps its colour and its name across a type change; the layer is a new object of the new type");
        p.getLayer(2)->colourIndex = 5;
        p.getLayer(2)->name = "Lead";
        expect(p.setLayerType(2, LayerTypeId::Kick));
        expect(p.typeAt(2) == LayerTypeId::Kick);
        expectEquals(p.getLayer(2)->colourIndex, 5);
        expectEquals(juce::String(p.getLayer(2)->name), juce::String("Lead"));
        expect(p.setLayerType(2, LayerTypeId::Empty));
        expect(p.typeAt(2) == LayerTypeId::Empty);
        expect(p.getLayer(2)->typeId() == 0);
        p.flushPendingAsyncUpdates();
    }

    void publishedViewAndSerial()
    {
        beginTest("each publication carries the serial the layout word then holds, and the view lists the new layer");
        auto box = std::make_unique<TypedHost>(); auto& p = *box;
        const auto serial0 = p.layoutSerial();
        expect(p.layerAccess.views.load()->serial == serial0);
        expect(p.setLayerType(1, LayerTypeId::Kick));
        const auto* view = p.layerAccess.views.load();
        expectEquals((int) (p.layoutSerial() - serial0), 2, "two publications");
        expect(view->serial == p.layoutSerial(), "the view and the layout word agree");
        expect(view->layer[1] == p.getLayer(1) && view->layer[1]->typeId() == (int) LayerTypeId::Kick);
        expectEquals((int) view->numLive, 4);
        expectEquals(p.layoutWordCountForTest(), 4);
        expectEquals(p.layerCountMirrorForTest(), 4);
        for (int s = 0; s < mu_limits::kMaxLayers; ++s) expect(view->layer[(size_t) s] == p.getLayer(s) || p.getLayer(s) == nullptr);
    }

    // The other strips are bit-identical to a run with no change, and slot 1 changes type between two blocks with
    // exactly one of the two layers rendering in every block.
    void otherSlotsUntouched()
    {
        beginTest("a type change in slot 1 mutes nothing: the other strips are bit-identical and each block has exactly one layer in slot 1");
        constexpr int kBlocks = 24, kBlock = 256, kChange = 10;
        ScopedTypeFlags flags;
        auto refBox = std::make_unique<TypedHost>(); auto& ref = *refBox;
        Run reference(ref, kBlocks * kBlock);
        for (int b = 0; b < kBlocks; ++b) reference.block(kBlock);

        auto box = std::make_unique<TypedHost>(); auto& p = *box;
        Run changed(p, kBlocks * kBlock);
        int armBlocks = 0, kickBlocks = 0, doubled = 0, neither = 0;
        for (int b = 0; b < kBlocks; ++b)
        {
            if (b == kChange) expect(p.setLayerType(1, LayerTypeId::Kick));
            gSlot1Arp = 0; gSlot1Kick = 0;
            changed.block(kBlock);
            const int a = gSlot1Arp.load(), k = gSlot1Kick.load();
            armBlocks += a > 0 ? 1 : 0;
            kickBlocks += k > 0 ? 1 : 0;
            if (a > 0 && k > 0) ++doubled;
            if (a == 0 && k == 0) ++neither;
        }
        expectEquals(doubled, 0, "never both layers in one block");
        expectEquals(neither, 0, "never neither");
        expectEquals(armBlocks, kChange);
        expectEquals(kickBlocks, kBlocks - kChange);
        for (int ch : { 0, 2, 3 })
            expect(streamsEqual(reference.tap, changed.tap, ch, 0, kBlocks * kBlock), "strip " + juce::String(ch) + " is bit-identical");
        expect(streamsEqual(reference.tap, changed.tap, 1, 0, kChange * kBlock), "slot 1 is unchanged up to the change");
        expect(! streamsEqual(reference.tap, changed.tap, 1, kChange * kBlock, kBlocks * kBlock), "and different after it");
        expectEquals(gUseAfterFree.load(), 0);
        expectEquals(p.layerAccess.views.overflowCount(), 0);
    }

    void retireTail()
    {
        beginTest("a layer with a tail sounds out in the bin after it leaves its slot, then is freed off the audio thread");
        constexpr int kBlock = 256;
        ScopedTypeFlags flags;
        auto box = std::make_unique<TypedHost>(); auto& p = *box;
        expect(p.setLayerType(1, LayerTypeId::Hat));
        Run run(p, 40 * kBlock);
        for (int b = 0; b < 4; ++b) run.block(kBlock);

        // The same history with an Arp in the slot instead: the difference is the tail.
        auto refBox = std::make_unique<TypedHost>(); auto& ref = *refBox;
        expect(ref.setLayerType(1, LayerTypeId::Kick));
        Run refRun(ref, 40 * kBlock);
        for (int b = 0; b < 4; ++b) refRun.block(kBlock);

        expect(p.setLayerType(1, LayerTypeId::Kick));
        expect(ref.setLayerType(1, LayerTypeId::Kick));   // a no-op: nothing leaves
        for (int b = 4; b < 40; ++b) { run.block(kBlock); refRun.block(kBlock); }

        const auto level = [&](const AppendTap& t, int block)
        {
            return t.bufs[1].getMagnitude(0, block * kBlock, kBlock);
        };
        expect(level(run.tap, 4) > level(refRun.tap, 4) + 0.01f, "the first block after the change carries the tail");
        expect(std::abs(level(run.tap, 39) - level(refRun.tap, 39)) < 1.0e-6f, "the tail has faded out by the end");
        expectEquals(p.retireBin.countIn(mu_core::RetireBin::Parked) + p.retireBin.countIn(mu_core::RetireBin::Draining), 0);
        p.flushPendingAsyncUpdates();
        expectEquals(p.retireBin.reclaimPending(), 0, "the old layer is freed");
    }

    void parametersAndStrip()
    {
        beginTest("the new layer's parameters start at their defaults or the initial node; the mixer strip is kept");
        auto box = std::make_unique<TypedHost>(); auto& p = *box;
        mu_seam_test::setParam(p.apvts, "v1_a", 0.9f);
        mu_seam_test::setParam(p.apvts, "ch1_lvl", 0.4f);
        expect(p.setLayerType(1, LayerTypeId::Kick));
        expectWithinAbsoluteError(mu_seam_test::getParam(p.apvts, "v1_a"), 0.25f, 0.001f);   // the default
        expectWithinAbsoluteError(mu_seam_test::getParam(p.apvts, "ch1_lvl"), 0.4f, 0.001f);   // the strip stays
        expectEquals(static_cast<FakeBeatLayer&>(*p.getLayer(1)).boundSlot, 1, "the new layer is bound to its slot");

        juce::ValueTree node(mu_pp::kLayerTag);
        juce::ValueTree row(mu_pp::kRowTag);
        row.setProperty("id", "a", nullptr);
        row.setProperty("x", 0.8, nullptr);
        node.appendChild(row, nullptr);
        expect(p.setLayerType(1, LayerTypeId::Arp, &node));
        expectWithinAbsoluteError(mu_seam_test::getParam(p.apvts, "v1_a"), 0.8f, 0.001f);
        expectWithinAbsoluteError(mu_seam_test::getParam(p.apvts, "ch1_lvl"), 0.4f, 0.001f);
    }

    void preparedForTheCurrentBlockSize()
    {
        beginTest("a prepareToPlay that lands while the new layer is built leaves it prepared for the new size (N1)");
        auto box = std::make_unique<TypedHost>(); auto& p = *box;
        p.prepareToPlay(48000.0, 256);
        std::function<void()> hook = [&p] { p.prepareToPlay(44100.0, 1024); };
        gOnCreate = &hook;
        const bool ok = p.setLayerType(2, LayerTypeId::Kick);
        gOnCreate = nullptr;
        expect(ok);
        auto& fresh = static_cast<FakeBeatLayer&>(*p.getLayer(2));
        expectEquals(fresh.preparedBlock, 1024);
        expectEquals(fresh.preparedRate, 44100.0);

        beginTest("a layer built after the host prepared is prepared at once; before any prepare it waits for it");
        expect(p.setLayerType(3, LayerTypeId::Kick));
        expectEquals(static_cast<FakeBeatLayer&>(*p.getLayer(3)).preparedBlock, 1024);
        auto fresh2 = std::make_unique<TypedHost>();
        expect(fresh2->setLayerType(0, LayerTypeId::Kick));
        expectEquals(static_cast<FakeBeatLayer&>(*fresh2->getLayer(0)).prepares, 0, "the host has not prepared: prepareToPlay will");
        fresh2->prepareToPlay(48000.0, 512);
        expectEquals(static_cast<FakeBeatLayer&>(*fresh2->getLayer(0)).preparedBlock, 512);
        p.releaseResources();
        fresh2->releaseResources();
    }

    void deleteEmptiesTheSlot()
    {
        beginTest("deleteLayer on a slot host empties the slot and nothing shifts");
        auto box = std::make_unique<TypedHost>(); auto& p = *box;
        const auto* neighbour = p.getLayer(2);
        expect(p.deleteLayer(1));
        expect(p.typeAt(1) == LayerTypeId::Empty);
        expect(p.getLayer(2) == neighbour);
        expectEquals(p.getNumLayers(), 4);
        expectEquals(p.layoutWordCountForTest(), 4);
    }

    // The list edits are TryLock work: on a Published host they are refused and change nothing; reset re-creates the slot.
    void listEditsRefused()
    {
        beginTest("a Published host refuses the list edits, leaves the view alone, and resets a slot without muting");
        auto box = std::make_unique<TypedHost>(); auto& p = *box;
        const auto* view = p.layerAccess.views.load();
        const auto serial = p.layoutSerial();
        expectEquals(p.addLayer(), -1);
        expect(! p.removeLayer(1));
        expect(! p.swapLayers(0, 1));
        expect(p.layerAccess.views.load() == view && p.layoutSerial() == serial);
        expect(! p.layerEditInFlight.load());

        expect(p.setLayerType(1, LayerTypeId::Kick));
        mu_seam_test::setParam(p.apvts, "ch1_lvl", 0.4f);
        p.getLayer(1)->name = "Lead";
        p.resetLayer(1);
        expect(p.typeAt(1) == LayerTypeId::Kick, "the type is kept");
        expectWithinAbsoluteError(mu_seam_test::getParam(p.apvts, "ch1_lvl"), 1.0f, 0.001f);   // the strip returns to default
        expect(p.getLayer(1)->name.empty());
        expect(! p.layerEditInFlight.load());
        p.flushPendingAsyncUpdates();
    }

    // Design step 3: the UI is told before the old layer is retired, while it is still the slot's layer.
    void uiLetsGoFirst()
    {
        beginTest("onLayerTypeChanging runs before the old layer leaves its slot, once per real change");
        auto box = std::make_unique<TypedHost>(); auto& p = *box;
        const auto* old = p.getLayer(2);
        int calls = 0, slot = -1;
        bool stillThere = false;
        p.onLayerTypeChanging = [&](int s) { ++calls; slot = s; stillThere = p.getLayer(s) == old && p.layerAccess.views.load()->layer[(size_t) s] == old; };
        expect(p.setLayerType(2, LayerTypeId::Arp));   // the type it has: no call
        expectEquals(calls, 0);
        expect(p.setLayerType(2, LayerTypeId::Kick));
        expectEquals(calls, 1);
        expectEquals(slot, 2);
        expect(stillThere, "the old layer was still in the slot and in the published view");
        p.onLayerTypeChanging = nullptr;
        p.flushPendingAsyncUpdates();
    }

    // The message thread changes types as fast as it can while a second thread renders blocks.
    void stress()
    {
        beginTest("two-thread stress: type changes while blocks run allocate nothing on the audio thread and need no overflow view");
        constexpr int kChanges = 1500;
        ScopedTypeFlags flags;
        auto box = std::make_unique<TypedHost>(8); auto& p = *box;
        p.setUseLayerRenderForTest(true);
        Driver d(p, 48000.0, 128);
        d.countAllocations = true;
        std::atomic<bool> stop { false };
        std::atomic<int> blocks { 0 };
        std::thread audio([&]
        {
            while (! stop.load()) { d.block(128); blocks.fetch_add(1); }
        });
        const LayerTypeId cycle[] = { LayerTypeId::Kick, LayerTypeId::Hat, LayerTypeId::Arp, LayerTypeId::Empty };
        for (int i = 0; i < kChanges; ++i)
        {
            expect(p.setLayerType(i % 6, cycle[(i / 6) % 4]));
            if (i % 50 == 0) p.flushPendingAsyncUpdates();
        }
        stop.store(true);
        audio.join();
        p.flushPendingAsyncUpdates();
        logMessage("type-change stress: " + juce::String(kChanges) + " changes, " + juce::String(blocks.load()) + " blocks, "
                   + juce::String(p.layerAccess.views.overflowCount()) + " overflow views");
        expectEquals(d.allocations + d.frees, 0, "the audio thread allocated or freed");
        // Overflow views are counted and logged above, not asserted: a descheduled audio thread legitimately forces one.
        expectEquals(gUseAfterFree.load(), 0, "no layer rendered after it was freed");
        expect(blocks.load() > 0);
    }
};
static LayerTypeSwapTests sLayerTypeSwapTests;
