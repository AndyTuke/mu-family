// swapLayers on a Published host (docs/mu-tronic/design.md 2.1.2, "Coexistence with ScopedSuspend structural edits"):
// two slots exchange their layers, mixer strips, tails and parameter rows behind a quiesce of the audio thread, proven
// on fake layer types before mu-Tronic reorders its sixteen slots. Invariants: a block never renders a layer at a slot
// it is not bound to, nor against another layer's parameter rows; the edit publishes exactly one view; the audio thread
// allocates nothing; a timeout of the grace wait moves nothing; a layer reference follows its layer; every strip, tail
// and sidechain source travels with its layer so the mix is the same as before the swap.
//
// Threads: the multi-thread test uses fixed iteration counts and asserts no timing.

#include "AllocationProbe.h"
#include "LayerContractFixtures.h"
#include "ProcessorSeamHelpers.h"
#include "Sequencer/LayerTypeRegistry.h"

#include <atomic>
#include <cmath>
#include <functional>
#include <thread>

using namespace mu_contract_test;

namespace
{
    using mu_core::LayerTypeId;

    std::atomic<int> gTorn { 0 }, gMixedParams { 0 }, gUseAfterFree { 0 };

    // A layer of two possible types: Kick (parameter prefix 'v', row "a") or Hat ('x', row "b", with a fading tail). It
    // renders a constant tone scaled by its OWN gain row, so a strip carrying it sounds the same wherever it sits, and it
    // checks on every render that it is bound to the slot it is rendered in and reads its own parameter row.
    struct TagLayer : Layer
    {
        TagLayer(LayerTypeId t, float toneValue) : type(t), tone(toneValue) {}
        ~TagLayer() override { magic = kDeadMagic; }

        int typeId() const override { return (int) type; }
        char letter() const noexcept { return type == LayerTypeId::Hat ? 'x' : 'v'; }

        void bindParams(juce::AudioProcessorValueTreeState& apvts, int slot) override
        {
            boundSlot.store(slot);
            ++binds;
            gain = mu_core::ParamBinding(apvts.getRawParameterValue(juce::String::charToString(letter()) + juce::String(slot) + "_"
                                                                    + (type == LayerTypeId::Hat ? "b" : "a")), this);
        }
        void onMoved(int i) override { movedTo = i; }

        void render(const mu_core::RenderContext& ctx, juce::AudioBuffer<float>& out, int n) override
        {
            if (magic != kAliveMagic) gUseAfterFree.fetch_add(1);
            renders.fetch_add(1);
            if (ctx.slot != boundSlot.load()) gTorn.fetch_add(1);
            const float g = gain.load();
            if (expectedGain >= 0.0f && std::abs(g - expectedGain) > 1.0e-5f) gMixedParams.fetch_add(1);
            if (! ctx.playing) return;
            for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::fill(out.getWritePointer(c), tone * (0.25f + g), n);
        }

        LayerTypeId type;
        float tone;
        float expectedGain = -1.0f;   // < 0: not checked
        mu_core::ParamBinding gain;
        std::atomic<int> boundSlot { -1 }, renders { 0 };
        int binds = 0, movedTo = -1;
        uint32_t magic = kAliveMagic;
    };

    struct HatLayer : TagLayer
    {
        explicit HatLayer(float toneValue) : TagLayer(LayerTypeId::Hat, toneValue) {}
        mu_core::RetirePolicy retirePolicy() const override { return { mu_core::RetirePolicy::Kind::Fade, 25.0f, 2.0f }; }
        bool renderRetiring(const mu_core::RenderContext&, juce::AudioBuffer<float>& out, int n, mu_core::RetireState& rs) override
        {
            for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::add(out.getWritePointer(c), 0.25f, n);
            mu_core::applyLinearFade(out, n, rs);
            return true;
        }
    };

    float toneOf(int index) { return 0.03f * (float) (index + 1); }
    LayerTypeId typeOf(int index) { return index % 3 == 2 ? LayerTypeId::Hat : LayerTypeId::Kick; }

    // A Published host whose slots alternate Kick, Kick, Hat; each slot carries both v{N}_a and x{N}_b rows and the
    // live prefix of its layer. Records what the structural hooks saw.
    struct SwapHost : FakeLayerHost
    {
        static juce::AudioProcessorValueTreeState::ParameterLayout layout()
        {
            auto l = makeLayout();
            for (int i = 0; i < mu_limits::kMaxLayers; ++i)
                l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { "x" + juce::String(i) + "_b", 1 }, "B",
                                                                  juce::NormalisableRange<float>(0.0f, 1.0f), 0.25f));
            return l;
        }

        explicit SwapHost(int live = 8) : FakeLayerHost(live, mu_core::LayerAccess::Published, false, layout())
        {
            std::vector<juce::StringArray> sets;
            for (int s = 0; s < mu_limits::kMaxLayers; ++s)
                sets.push_back(juce::StringArray { "v" + juce::String(s) + "_", "x" + juce::String(s) + "_" });
            initLayerStatePrefixSets(sets);

            auto offer = [this](LayerTypeId id, const char* name, char letter, mu_core::LayerTypeInfo::CreateFn fn)
            {
                mu_core::LayerTypeInfo i;
                i.id = id; i.name = name; i.group = "Test"; i.create = fn; i.paramPrefixLetter = letter;
                layerTypes.add(i);
            };
            offer(LayerTypeId::Kick, "Kick", 'v', [](int slot, LayerTypeId) -> std::unique_ptr<Layer> { return std::make_unique<TagLayer>(LayerTypeId::Kick, toneOf(slot)); });
            offer(LayerTypeId::Hat,  "Hat",  'x', [](int slot, LayerTypeId) -> std::unique_ptr<Layer> { return std::make_unique<HatLayer>(toneOf(slot)); });

            for (int i = 0; i < mu_limits::kMaxLayers; ++i)
            {
                std::unique_ptr<TagLayer> l = typeOf(i) == LayerTypeId::Hat ? std::make_unique<HatLayer>(toneOf(i))
                                                                           : std::make_unique<TagLayer>(LayerTypeId::Kick, toneOf(i));
                l->colourIndex = i;
                l->name = "L" + std::to_string(i);
                layers[(size_t) i] = std::move(l);
                layers[(size_t) i]->bindParams(apvts, i);
                layerLayout.setLivePrefix(i, juce::String::charToString(tag(i).letter()) + juce::String(i) + "_");
                // Every layer's own row holds a value of its own, which it checks it still reads after a move.
                setRow(i, 0.1f + 0.05f * (float) i);
            }
            publishLayers(live);
            flushPendingAsyncUpdates();
        }

        TagLayer& tag(int s) { return static_cast<TagLayer&>(*layers[(size_t) s]); }
        juce::String livePrefix(int s) const { return layerLayout.prefix(s); }
        void setRow(int slot, float value)
        {
            mu_seam_test::setParam(apvts, juce::String::charToString(tag(slot).letter()) + juce::String(slot) + "_"
                                              + (tag(slot).type == LayerTypeId::Hat ? "b" : "a"), value);
            tag(slot).expectedGain = value;
        }
        float row(int slot) { return mu_seam_test::getParam(apvts, juce::String::charToString(tag(slot).letter()) + juce::String(slot) + "_"
                                                                      + (tag(slot).type == LayerTypeId::Hat ? "b" : "a")); }

        bool layersAreSlots() const override { return true; }
        std::unique_ptr<Layer> createLayer(int) override { return std::make_unique<TagLayer>(LayerTypeId::Kick, 0.0f); }

        // What the hooks saw.
        void remapLayerReferences(const std::function<int(int)>& oldToNew) override
        {
            ++hookCalls;
            hookFlagUp = layerEditInFlight.load();
            for (int j = 0; j < mu_limits::kMaxLayers; ++j)
            {
                hookTone[(size_t) j] = layers[(size_t) j] != nullptr ? static_cast<TagLayer&>(*layers[(size_t) j]).tone : -1.0f;
                hookMap[(size_t) j] = oldToNew(j);
            }
        }
        void layersMoved(int first, int last) override
        {
            movedCalls.emplace_back(first, last);
            movedFlagUp = layerEditInFlight.load();
            if (inEdit) inEdit();
        }

        int hookCalls = 0;
        bool hookFlagUp = false, movedFlagUp = false;
        std::array<float, mu_limits::kMaxLayers> hookTone {};
        std::array<int, mu_limits::kMaxLayers> hookMap {};
        std::vector<std::pair<int, int>> movedCalls;
        std::function<void()> inEdit;
    };

    int activeTails(SwapHost& p, int row)
    {
        int n = 0;
        p.retireBin.forEachActive(row, [&n](mu_core::RetireTail&) { ++n; });
        return n;
    }
}

struct LayerSwapPublishedTests : juce::UnitTest
{
    LayerSwapPublishedTests() : juce::UnitTest("swapLayers: reorder on a Published host (quiesce)", "mu-core") {}

    void runTest() override
    {
        refusalsAndOnePublication();
        layerIdentityTravels();
        rowsAndLivePrefixes();
        stripsAndSidechains();
        referencesAndTheFlag();
        timeoutMovesNothing();
        tailFollowsItsStrip();
        mixIsUnchanged();
        stress();
    }

    void refusalsAndOnePublication()
    {
        beginTest("a bad pair or a host that cannot edit is refused; a swap publishes exactly one view and lowers the flag");
        auto box = std::make_unique<SwapHost>(); auto& p = *box;
        expect(! p.swapLayers(-1, 2));
        expect(! p.swapLayers(1, 8), "past the live count");
        expect(! p.swapLayers(3, 3));
        p.editable = false;
        expect(! p.swapLayers(0, 1), "canEditLayers is false");
        p.editable = true;
        expect(! p.layerEditInFlight.load());

        const auto serial0 = p.layoutSerial();
        const auto* view0 = p.layerAccess.views.load();
        auto* l1 = p.getLayer(1);
        auto* l3 = p.getLayer(3);
        expect(p.swapLayers(1, 3));
        const auto* view = p.layerAccess.views.load();
        expect(view != view0);
        expectEquals((int) (p.layoutSerial() - serial0), 1, "one publication");
        expect(view->serial == p.layoutSerial(), "the view and the layout word agree");
        expectEquals((int) view->numLive, 8);
        expect(view->layer[1] == l3 && view->layer[3] == l1, "the view lists the exchanged layers");
        expect(p.getLayer(1) == l3 && p.getLayer(3) == l1);
        for (int s = 0; s < mu_limits::kMaxLayers; ++s) expect(view->layer[(size_t) s] == p.layers[(size_t) s].get(), "slot " + juce::String(s));
        expect(! p.layerEditInFlight.load());
        expectEquals(p.layoutWordCountForTest(), 8);
        expectEquals(p.layerCountMirrorForTest(), 8);
        expectEquals(p.layerAccess.views.overflowCount(), 0);
        expectEquals((int) p.movedCalls.size(), 2, "layersMoved once per exchanged slot");
        expect(p.movedFlagUp, "layersMoved ran while blocks were kept out");
    }

    void layerIdentityTravels()
    {
        beginTest("the layer keeps its name, colour and object; it is told its new slot and re-binds there");
        auto box = std::make_unique<SwapHost>(); auto& p = *box;
        auto& a = p.tag(1);
        auto& b = p.tag(4);
        const int bindsA = a.binds, bindsB = b.binds;
        int uiCalls = 0;
        p.onLayerTypeChanging = [&uiCalls](int) { ++uiCalls; };
        expect(p.swapLayers(1, 4));
        p.onLayerTypeChanging = nullptr;
        expectEquals(uiCalls, 2, "the editor is told about both slots");
        expect(p.getLayer(4) == &a && p.getLayer(1) == &b);
        expectEquals(juce::String(a.name), juce::String("L1"));
        expectEquals(a.colourIndex, 1);
        expectEquals(juce::String(b.name), juce::String("L4"));
        expectEquals(b.colourIndex, 4);
        expectEquals(a.movedTo, 4);
        expectEquals(b.movedTo, 1);
        expectEquals(a.boundSlot.load(), 4);
        expectEquals(b.boundSlot.load(), 1);
        expectEquals(a.binds, bindsA + 1);
        expectEquals(b.binds, bindsB + 1);

        beginTest("a swap back restores the original arrangement and bindings");
        expect(p.swapLayers(1, 4));
        expect(p.getLayer(1) == &a && p.getLayer(4) == &b);
        expectEquals(a.boundSlot.load(), 1);
        p.flushPendingAsyncUpdates();
    }

    void rowsAndLivePrefixes()
    {
        beginTest("the rows follow the layer: same prefix letters exchange, different letters land under the layer's own letter");
        auto box = std::make_unique<SwapHost>(); auto& p = *box;
        // Slots 0 and 1 are Kick ('v'), slot 2 is Hat ('x'): the values were set per slot in the constructor.
        const float r0 = p.row(0), r1 = p.row(1), r2 = p.row(2);
        expect(p.swapLayers(0, 1));
        expectWithinAbsoluteError(mu_seam_test::getParam(p.apvts, "v0_a"), r1, 1.0e-4f);
        expectWithinAbsoluteError(mu_seam_test::getParam(p.apvts, "v1_a"), r0, 1.0e-4f);
        expectEquals(p.livePrefix(0), juce::String("v0_"));
        expectEquals(p.livePrefix(1), juce::String("v1_"));

        // Kick (now in slot 1) with Hat (slot 2).
        expect(p.swapLayers(1, 2));
        expectEquals(p.livePrefix(1), juce::String("x1_"), "slot 1 holds the Hat: its letter is live there");
        expectEquals(p.livePrefix(2), juce::String("v2_"), "slot 2 holds the Kick");
        expectWithinAbsoluteError(mu_seam_test::getParam(p.apvts, "x1_b"), r2, 1.0e-4f);
        expectWithinAbsoluteError(mu_seam_test::getParam(p.apvts, "v2_a"), r0, 1.0e-4f);

        // The saved state carries each slot's live rows only.
        const auto state = p.captureComposedState();
        const auto rowOf = [&](int slot, const char* id)
        {
            const auto node = mu_pp::findLayerNode(state, slot);
            for (int i = 0; i < node.getNumChildren(); ++i)
                if (const auto row = node.getChild(i); row.hasType(mu_pp::kRowTag) && row.getProperty("id").toString() == id)
                    return (float) (double) row.getProperty("x");
            return -1.0f;
        };
        expectWithinAbsoluteError(rowOf(1, "b"), r2, 1.0e-4f);
        expectWithinAbsoluteError(rowOf(2, "a"), r0, 1.0e-4f);
        expectEquals(rowOf(1, "a"), -1.0f, "the dormant letter is not written");

        // Everything the layers read is their own after the swaps.
        Driver d(p);
        p.setUseLayerRenderForTest(true);
        gTorn = 0; gMixedParams = 0;
        for (int b = 0; b < 4; ++b) d.block(256);
        expectEquals(gTorn.load(), 0);
        expectEquals(gMixedParams.load(), 0);
        p.flushPendingAsyncUpdates();
    }

    void stripsAndSidechains()
    {
        beginTest("the mixer strip travels with its layer: level, pan, output bus, sidechain follower; sources re-point");
        auto box = std::make_unique<SwapHost>(); auto& p = *box;
        mu_seam_test::setParam(p.apvts, "ch1_lvl", 0.3f);
        mu_seam_test::setParam(p.apvts, "ch3_lvl", 0.8f);
        mu_seam_test::setParam(p.apvts, "ch1_pan", -0.5f);
        mu_seam_test::setParam(p.apvts, "ch3_pan", 0.25f);
        mu_seam_test::setParam(p.apvts, "ch1_outBus", 2.0f);
        mu_seam_test::setParam(p.apvts, "ch0_scSrc", 2.0f);   // strip 0 ducks from layer 1 (sources are 1-based)
        mu_seam_test::setParam(p.apvts, "ch5_scSrc", 4.0f);   // strip 5 ducks from layer 3
        mu_seam_test::setParam(p.apvts, "ch6_scSrc", 7.0f);   // strip 6 ducks from layer 6: untouched by the swap
        p.flushPendingAsyncUpdates();
        p.mixerEngine.setSidechainEnv(1, 0.9f);
        p.mixerEngine.setSidechainEnv(3, 0.2f);

        expect(p.swapLayers(1, 3));
        p.flushPendingAsyncUpdates();

        expectWithinAbsoluteError(mu_seam_test::getParam(p.apvts, "ch3_lvl"), 0.3f, 1.0e-4f);
        expectWithinAbsoluteError(mu_seam_test::getParam(p.apvts, "ch1_lvl"), 0.8f, 1.0e-4f);
        expectWithinAbsoluteError(mu_seam_test::getParam(p.apvts, "ch3_pan"), -0.5f, 1.0e-4f);
        expectWithinAbsoluteError(mu_seam_test::getParam(p.apvts, "ch1_pan"), 0.25f, 1.0e-4f);
        expectEquals(juce::roundToInt(mu_seam_test::getParam(p.apvts, "ch3_outBus")), 2);
        expectEquals(juce::roundToInt(mu_seam_test::getParam(p.apvts, "ch1_outBus")), 0);
        expectWithinAbsoluteError(p.mixerEngine.channels[3].level.load(), 0.3f, 1.0e-4f);
        expectWithinAbsoluteError(p.mixerEngine.channels[1].level.load(), 0.8f, 1.0e-4f);
        expectEquals(p.mixerEngine.channels[3].outputBus.load(), 2);
        expectWithinAbsoluteError(p.mixerEngine.getSidechainEnv(3), 0.9f, 1.0e-6f);
        expectWithinAbsoluteError(p.mixerEngine.getSidechainEnv(1), 0.2f, 1.0e-6f);
        expectEquals(juce::roundToInt(mu_seam_test::getParam(p.apvts, "ch0_scSrc")), 4, "a strip that ducked from layer 1 follows it to slot 3");
        expectEquals(juce::roundToInt(mu_seam_test::getParam(p.apvts, "ch5_scSrc")), 2, "and one that ducked from layer 3 follows it to slot 1");
        expectEquals(juce::roundToInt(mu_seam_test::getParam(p.apvts, "ch6_scSrc")), 7);
        expectEquals(p.mixerEngine.channels[0].sidechainSource.load(), 3, "the engine holds the 0-based layer");
    }

    void referencesAndTheFlag()
    {
        beginTest("remapLayerReferences runs before the layers move, in the flagged window; a block in the window renders nothing");
        auto box = std::make_unique<SwapHost>(); auto& p = *box;
        Driver d(p, 48000.0, 128);
        p.setUseLayerRenderForTest(true);
        d.block(128);
        d.block(128);
        int rendersInWindow = -1;
        std::array<int, 8> rendersBefore {};
        for (int s = 0; s < 8; ++s) rendersBefore[(size_t) s] = p.tag(s).renders.load();
        p.inEdit = [&]
        {
            d.block(128);   // a block that starts mid-edit (the audio thread on another thread would do the same)
            rendersInWindow = 0;
            for (int s = 0; s < 8; ++s) rendersInWindow += p.tag(s).renders.load() - rendersBefore[(size_t) s];
        };
        const float toneAt1 = p.tag(1).tone, toneAt4 = p.tag(4).tone;
        expect(p.swapLayers(1, 4));
        p.inEdit = nullptr;
        expectEquals(p.hookCalls, 1);
        expect(p.hookFlagUp, "blocks were already kept out when the hook ran");
        expectEquals(p.hookTone[1], toneAt1, "the hook saw the old layer at slot 1");
        expectEquals(p.hookTone[4], toneAt4, "and the old layer at slot 4");
        expectEquals(p.hookMap[1], 4);
        expectEquals(p.hookMap[4], 1);
        expectEquals(p.hookMap[2], 2);
        expectEquals(rendersInWindow, 0, "no layer rendered while the edit was in flight");
        d.block(128);
        int after = 0;
        for (int s = 0; s < 8; ++s) after += p.tag(s).renders.load() - rendersBefore[(size_t) s];
        expect(after > 0, "and they render again once the flag is down");
        p.flushPendingAsyncUpdates();
    }

    void timeoutMovesNothing()
    {
        beginTest("a block that never ends makes the edit give up after the grace timeout: nothing moved, flag lowered");
        auto box = std::make_unique<SwapHost>(); auto& p = *box;
        std::atomic<bool> inside { false }, release { false };
        std::thread audio([&]
        {
            p.layerAccess.epoch.enter();
            inside.store(true);
            while (! release.load()) std::this_thread::yield();
            p.layerAccess.epoch.leave();
        });
        while (! inside.load()) std::this_thread::yield();

        auto* l1 = p.getLayer(1);
        auto* l2 = p.getLayer(2);
        const auto serial = p.layoutSerial();
        const auto started = juce::Time::getMillisecondCounter();
        expect(! p.swapLayers(1, 2));
        expect(juce::Time::getMillisecondCounter() - started >= 40, "it waited");
        expect(p.getLayer(1) == l1 && p.getLayer(2) == l2, "nothing moved");
        expect(p.layoutSerial() == serial, "nothing was published");
        expect(! p.layerEditInFlight.load(), "the flag is lowered again");
        expect(p.movedCalls.empty());
        expectEquals(p.hookCalls, 0, "no parameter was touched");

        release.store(true);
        audio.join();
        expect(p.swapLayers(1, 2), "the next attempt, with the block gone, succeeds");
        p.flushPendingAsyncUpdates();
    }

    void tailFollowsItsStrip()
    {
        beginTest("a tail owed to a strip moves with the strip and sounds out there");
        auto box = std::make_unique<SwapHost>(); auto& p = *box;
        expect(p.setLayerType(1, LayerTypeId::Hat));
        Driver d(p, 48000.0, 128);
        p.setUseLayerRenderForTest(true);
        for (int b = 0; b < 3; ++b) d.block(128);
        expect(p.setLayerType(1, LayerTypeId::Kick), "the Hat leaves slot 1 and fades out in the bin");
        d.block(128);
        expectEquals(activeTails(p, 1), 1);

        expect(p.swapLayers(1, 5));
        expectEquals(activeTails(p, 1), 0, "the tail left row 1 with the strip");
        expectEquals(activeTails(p, 5), 1, "and is in row 5");
        for (int b = 0; b < 60; ++b) d.block(128);
        p.flushPendingAsyncUpdates();
        expectEquals(p.retireBin.countIn(mu_core::RetireBin::Parked) + p.retireBin.countIn(mu_core::RetireBin::Draining), 0);
        expectEquals(p.retireBin.reclaimPending(), 0, "the faded tail was freed");
        expectEquals(gUseAfterFree.load(), 0);
    }

    // The same layers and strips with the pair swapped between every block sum to the same master as with no swap:
    // every strip carries its layer, so reordering moves nothing in the mix. The step detector watches the swapped run
    // alone, so a click would show even if the reference were wrong.
    void mixIsUnchanged()
    {
        beginTest("swapping between every block leaves the mix unchanged, at block sizes 1, 7, 64 and 512");
        for (const int blockSize : { 1, 7, 64, 512 })
        {
            const int blocks = blockSize == 1 ? 400 : 200;
            auto refBox = std::make_unique<SwapHost>(); auto& ref = *refBox;
            auto box = std::make_unique<SwapHost>(); auto& p = *box;
            for (auto* host : { &ref, &p })
                for (int s = 0; s < 8; ++s)
                {
                    mu_seam_test::setParam(host->apvts, "ch" + juce::String(s) + "_lvl", 0.3f + 0.08f * (float) s);
                    mu_seam_test::setParam(host->apvts, "ch" + juce::String(s) + "_pan", -0.7f + 0.2f * (float) s);
                }
            ref.flushPendingAsyncUpdates();
            p.flushPendingAsyncUpdates();
            ref.setUseLayerRenderForTest(true);
            p.setUseLayerRenderForTest(true);
            Driver dr(ref, 48000.0, 512), dp(p, 48000.0, 512);
            gTorn = 0; gMixedParams = 0;

            float worstDiff = 0.0f, worstStep = 0.0f, worstRefStep = 0.0f, lastL = 0.0f, lastRefL = 0.0f;
            std::uint32_t rng = 12345u;
            for (int b = 0; b < blocks; ++b)
            {
                rng = rng * 1664525u + 1013904223u;
                const int x = (int) ((rng >> 8) % 8u);
                rng = rng * 1664525u + 1013904223u;
                const int y = (int) ((rng >> 8) % 8u);
                if (x != y) expect(p.swapLayers(x, y));
                const auto& outRef = dr.block(blockSize);
                const auto& out = dp.block(blockSize);
                for (int i = 0; i < blockSize; ++i)
                    for (int c = 0; c < 2; ++c)
                        worstDiff = juce::jmax(worstDiff, std::abs(out.getSample(c, i) - outRef.getSample(c, i)));
                for (int i = 0; i < blockSize; ++i)
                {
                    worstStep = juce::jmax(worstStep, std::abs(out.getSample(0, i) - lastL));
                    worstRefStep = juce::jmax(worstRefStep, std::abs(outRef.getSample(0, i) - lastRefL));
                    lastL = out.getSample(0, i);
                    lastRefL = outRef.getSample(0, i);
                }
            }
            expect(worstDiff < 1.0e-5f, "block " + juce::String(blockSize) + ": the swapped mix equals the reference (worst " + juce::String(worstDiff, 8) + ")");
            expect(worstStep <= worstRefStep + 1.0e-5f, "block " + juce::String(blockSize) + ": no step the reference does not have (" + juce::String(worstStep, 6)
                                                       + " vs " + juce::String(worstRefStep, 6) + ")");
            expectEquals(gTorn.load(), 0);
            expectEquals(gMixedParams.load(), 0);
            expectEquals(p.layerAccess.views.overflowCount(), 0);
            p.flushPendingAsyncUpdates();
        }
    }

    // The message thread swaps as fast as it can while a second thread renders blocks.
    void stress()
    {
        beginTest("two-thread stress: swaps while blocks run allocate nothing, never tear a layer from its slot or its rows");
        constexpr int kSwaps = 1500;
        gTorn = 0; gMixedParams = 0; gUseAfterFree = 0;
        auto box = std::make_unique<SwapHost>(8); auto& p = *box;
        p.setUseLayerRenderForTest(true);
        Driver d(p, 48000.0, 128);
        d.countAllocations = true;
        std::atomic<bool> stop { false };
        std::atomic<int> blocks { 0 };
        std::thread audio([&]
        {
            while (! stop.load()) { d.block(128); blocks.fetch_add(1); }
        });
        std::uint32_t rng = 777u;
        int done = 0, refused = 0;
        for (int i = 0; i < kSwaps; ++i)
        {
            rng = rng * 1664525u + 1013904223u;
            const int x = (int) ((rng >> 8) % 8u);
            rng = rng * 1664525u + 1013904223u;
            const int y = (int) ((rng >> 8) % 8u);
            if (x == y) continue;
            if (p.swapLayers(x, y)) ++done; else ++refused;
            if (i % 50 == 0) p.flushPendingAsyncUpdates();
        }
        stop.store(true);
        audio.join();
        p.flushPendingAsyncUpdates();
        logMessage("swap stress: " + juce::String(done) + " swaps, " + juce::String(refused) + " refused, " + juce::String(blocks.load()) + " blocks, "
                   + juce::String(p.layerAccess.views.overflowCount()) + " overflow views");
        expectEquals(d.allocations + d.frees, 0, "the audio thread allocated or freed");
        expectEquals(gTorn.load(), 0, "no block rendered a layer in a slot it was not bound to");
        expectEquals(gMixedParams.load(), 0, "no block read another layer's rows");
        expectEquals(gUseAfterFree.load(), 0);
        expect(blocks.load() > 0 && done > 0);
        for (int s = 0; s < 8; ++s)
        {
            expectEquals(p.tag(s).boundSlot.load(), s);
            expectWithinAbsoluteError(p.row(s), p.tag(s).expectedGain, 1.0e-4f);
            expect(p.layerAccess.views.load()->layer[(size_t) s] == p.getLayer(s));
        }
        expect(! p.layerEditInFlight.load());
    }
};
static LayerSwapPublishedTests sLayerSwapPublishedTests;
