#include "TestProcessorCore.h"
#include "TestProcessorBase.h"
#include "Plugin/MixerFxParams.h"

#include <thread>
#include <utility>
#include <vector>

namespace
{
    // A layer that records when it is destroyed and whether the lock was held at that moment, to
    // prove a removed or replaced layer is released after layersLock is dropped. The probe asks
    // another thread to take the lock: a critical section is re-entrant for its own owner, so only
    // a second thread can tell whether this one still holds it.
    struct ProbeLayer : Layer
    {
        ProbeLayer(juce::CriticalSection& l, int& destroyedCount, int& underLockCount, int& resetCount)
            : lock(l), destroyed(destroyedCount), underLock(underLockCount), resets(resetCount) {}
        ~ProbeLayer() override
        {
            ++destroyed;
            bool acquired = false;
            std::thread probe([&] { acquired = lock.tryEnter(); if (acquired) lock.exit(); });
            probe.join();
            if (! acquired) ++underLock;
        }
        void adoptDefaults(Layer& fresh) override { ++resets; Layer::adoptDefaults(fresh); }
        void onMoved(int i) override    { movedTo = i; }

        juce::CriticalSection& lock;
        int& destroyed;
        int& underLock;
        int& resets;
        int  movedTo = -1;
    };

    // A ProcessorBase with the real mixer-strip parameters and a layer layout, whose layers are
    // ProbeLayers, so ProcessorBase::addLayer and the other structural edits run end to end.
    struct EditableProcessor : ProcessorBase
    {
        explicit EditableProcessor(bool spares)
            : ProcessorBase(BusesProperties().withOutput("Out", juce::AudioChannelSet::stereo()), makeLayout())
        {
            keepSpareLayers = spares;
            juce::StringArray prefixes;
            for (int i = 0; i < mu_limits::kMaxLayers; ++i) prefixes.add("v" + juce::String(i) + "_");
            initLayerState(prefixes);
            for (int i = 0; i < (spares ? mu_limits::kMaxLayers : 1); ++i)
                layers[(size_t) i] = createLayer(i);
            numLayers.store(1);
            destroyed = underLock = resets = 0;   // the setup built layers and destroyed none
        }

        static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
        {
            juce::AudioProcessorValueTreeState::ParameterLayout layout;
            for (int i = 0; i < mu_limits::kMaxLayers; ++i)
            {
                layout.add(std::make_unique<juce::AudioParameterFloat>(
                    juce::ParameterID { "v" + juce::String(i) + "_a", 1 }, "A",
                    juce::NormalisableRange<float>(0.0f, 1.0f), 0.25f));
                mu_mixfx::addChannelStripParams(layout, "ch" + juce::String(i) + "_", "Ch ");
            }
            return layout;
        }

        // The layers' destructors use members of this class and of ProcessorCore (the lock), which are
        // destroyed before the base releases the array, so release the layers while they are alive.
        ~EditableProcessor() override { for (auto& l : layers) l.reset(); }

        using ProcessorCore::layers;
        using ProcessorCore::layersLock;
        using ProcessorBase::layerSwapper;
        bool canEditLayers() const override { return true; }
        std::unique_ptr<Layer> createLayer(int) override
        {
            return std::make_unique<ProbeLayer>(layersLock, destroyed, underLock, resets);
        }
        void cancelPendingSwap(int slot) override { cancelled.push_back(slot); ProcessorBase::cancelPendingSwap(slot); }
        void layersMoved(int first, int last) override
        {
            moved.push_back({ first, last });
            movedHoldingLock.push_back(lockHeldByOtherThread() ? 1 : 0);
        }
        // A critical section is re-entrant for its owner, so only a second thread can tell it is held.
        bool lockHeldByOtherThread()
        {
            bool acquired = false;
            std::thread probeThread([&] { acquired = layersLock.tryEnter(); if (acquired) layersLock.exit(); });
            probeThread.join();
            return ! acquired;
        }

        float param(const juce::String& id) const
        {
            auto* p = apvts.getParameter(id);
            return p->convertFrom0to1(p->getValue());
        }
        void setParam(const juce::String& id, float actual)
        {
            auto* p = apvts.getParameter(id);
            p->setValueNotifyingHost(p->convertTo0to1(actual));
        }
        ProbeLayer* probe(int i) { return static_cast<ProbeLayer*>(layers[(size_t) i].get()); }

        int destroyed = 0, underLock = 0, resets = 0;
        std::vector<int> cancelled;
        std::vector<std::pair<int, int>> moved;   // layersMoved(first, last) calls, in order
        std::vector<int> movedHoldingLock;        // 1 when the call came with layersLock held

        juce::String getChannelName(int) const override { return {}; }
        int getChannelColourIndex(int i) const override { return layers[(size_t) i] ? layers[(size_t) i]->colourIndex : 0; }
        juce::File   getLayerPresetDir()       const override { return {}; }
        juce::String getLayerPresetExtension() const override { return ".muTest"; }
        juce::String getFullPresetExtension()  const override { return ".muTest"; }
        void applyFullMidiPreset(const juce::File&) override {}

        const juce::String getName() const override          { return "edit-test"; }
        void prepareToPlay(double, int) override             {}
        void releaseResources() override                      {}
        void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
        juce::AudioProcessorEditor* createEditor() override   { return nullptr; }
        bool hasEditor() const override                       { return false; }
        bool acceptsMidi() const override                     { return false; }
        bool producesMidi() const override                    { return false; }
        double getTailLengthSeconds() const override          { return 0.0; }
        int getNumPrograms() override                         { return 1; }
        int getCurrentProgram() override                      { return 0; }
        void setCurrentProgram(int) override                  {}
        const juce::String getProgramName(int) override       { return {}; }
        void changeProgramName(int, const juce::String&) override {}
        void getStateInformation(juce::MemoryBlock&) override {}
        void setStateInformation(const void*, int) override   {}
    };

    // A derived layer that counts its destructions, to prove who releases a layer and when.
    struct CountedLayer : Layer
    {
        explicit CountedLayer(int& destroyed) : counter(destroyed) {}
        ~CountedLayer() override { ++counter; }
        int& counter;
    };
}

// ProcessorCore's layer storage: construct, publish, typed access, and destruction off the lock.
struct LayerOwnershipTests : juce::UnitTest
{
    LayerOwnershipTests() : juce::UnitTest("ProcessorCore layer ownership", "mu-core") {}

    void runTest() override
    {
        beginTest("layerAs returns the derived layer, including slots above the live count");
        {
            int destroyed = 0;
            TestProcessorCore p(1);
            p.layers[5] = std::make_unique<CountedLayer>(destroyed);
            expect(p.getLayer(5) == nullptr, "a slot above the count is not live");
            expect(&p.layerAs<CountedLayer>(5) == p.layers[5].get());
            expect(&p.layerAs<Layer>(0) == p.getLayer(0));
        }

        beginTest("publishing a layer makes it visible through the count");
        {
            int destroyed = 0;
            TestProcessorCore p(2);
            p.layers[2] = std::make_unique<CountedLayer>(destroyed);
            expect(p.getLayer(2) == nullptr);
            {
                const juce::ScopedLock sl(p.layersLock);
                p.numLayers.store(3, std::memory_order_release);
            }
            expectEquals(p.getNumChannels(), 3);
            expect(p.getLayer(2) == p.layers[2].get());
        }

        beginTest("a removed layer is destroyed after the lock is released, not under it");
        {
            int destroyed = 0;
            TestProcessorCore p(1);
            p.layers[1] = std::make_unique<CountedLayer>(destroyed);
            p.numLayers.store(2);

            std::unique_ptr<Layer> removed;
            {
                const juce::ScopedLock sl(p.layersLock);
                removed = std::move(p.layers[1]);
                p.numLayers.store(1, std::memory_order_release);
            }
            expectEquals(destroyed, 0, "still alive while held outside the lock");
            expect(p.getLayer(1) == nullptr);
            removed.reset();
            expectEquals(destroyed, 1);
        }

        beginTest("the processor destroys the layers it still owns");
        {
            int destroyed = 0;
            {
                TestProcessorCore p(0);
                for (int i = 0; i < 4; ++i) p.layers[(size_t) i] = std::make_unique<CountedLayer>(destroyed);
            }
            expectEquals(destroyed, 4);
        }

        runStoragePrimitiveTests();
        runStructuralEditTests();
    }

    // Parameter values go through a normalised round trip, so compare to a tolerance, not exactly.
    void expectNear(float actual, float expected) { expectWithinAbsoluteError(actual, expected, 1.0e-3f); }

    // The pointer moves under layersLock: they move pointers and counts, and release nothing.
    void runStoragePrimitiveTests()
    {
        beginTest("publishLayerLocked grows the count and hands back the spare it replaced");
        {
            int destroyed = 0;
            TestProcessorCore p(2);
            p.layers[2] = std::make_unique<CountedLayer>(destroyed);   // a spare above the count
            auto* spare = p.layers[2].get();
            auto fresh = std::make_unique<CountedLayer>(destroyed);
            auto* freshPtr = fresh.get();
            std::unique_ptr<Layer> replaced;
            {
                const juce::ScopedLock sl(p.layersLock);
                replaced = p.publishLayerLocked(std::move(fresh));
            }
            expectEquals(p.getNumChannels(), 3);
            expect(p.getLayer(2) == freshPtr);
            expect(replaced.get() == spare);
            expectEquals(destroyed, 0, "the caller releases the spare, not the primitive");
        }

        beginTest("unpublishLayerLocked shifts the layers above down by pointer and fills the top slot");
        {
            int destroyed = 0;
            TestProcessorCore p(0);
            Layer* ptr[4];
            for (int i = 0; i < 4; ++i)
            {
                auto l = std::make_unique<CountedLayer>(destroyed);
                ptr[i] = l.get();
                p.layers[(size_t) i] = std::move(l);
            }
            p.numLayers.store(4);
            auto spare = std::make_unique<CountedLayer>(destroyed);
            auto* sparePtr = spare.get();

            std::unique_ptr<Layer> removed;
            {
                const juce::ScopedLock sl(p.layersLock);
                removed = p.unpublishLayerLocked(1, std::move(spare));
            }
            expectEquals(p.getNumChannels(), 3);
            expect(removed.get() == ptr[1]);
            expect(p.layers[0].get() == ptr[0]);
            expect(p.layers[1].get() == ptr[2], "the layer above moved down, not copied");
            expect(p.layers[2].get() == ptr[3]);
            expect(p.layers[3].get() == sparePtr, "the vacated top slot holds the spare");
            expectEquals(destroyed, 0);
            removed.reset();
            expectEquals(destroyed, 1);
        }

        beginTest("unpublishLayerLocked without a spare leaves the vacated slot empty");
        {
            int destroyed = 0;
            TestProcessorCore p(0);
            for (int i = 0; i < 3; ++i) p.layers[(size_t) i] = std::make_unique<CountedLayer>(destroyed);
            p.numLayers.store(3);
            std::unique_ptr<Layer> removed;
            {
                const juce::ScopedLock sl(p.layersLock);
                removed = p.unpublishLayerLocked(2, nullptr);
            }
            expectEquals(p.getNumChannels(), 2);
            expect(p.layers[2] == nullptr);
        }

        beginTest("swapLayersLocked exchanges the pointers");
        {
            TestProcessorCore p;
            auto* a = p.layers[0].get();
            auto* c = p.layers[2].get();
            {
                const juce::ScopedLock sl(p.layersLock);
                p.swapLayersLocked(0, 2);
            }
            expect(p.layers[0].get() == c);
            expect(p.layers[2].get() == a);
            expectEquals(p.getNumChannels(), 3);
        }
    }

    // ProcessorBase::addLayer / removeLayer / swapLayers / resetLayer end to end.
    void runStructuralEditTests()
    {
        beginTest("a product with a fixed count refuses every structural edit");
        {
            TestProcessorBase p(juce::AudioProcessor::wrapperType_Undefined);
            expectEquals(p.addLayer(), -1);
            expect(! p.removeLayer(0));
            expect(! p.swapLayers(0, 1));
        }

        beginTest("addLayer fills the slots in order, each with a distinct colour, and refuses past the maximum");
        {
            EditableProcessor p(false);
            p.getLayer(0)->colourIndex = 0;
            for (int i = 1; i < mu_limits::kMaxLayers; ++i)
            {
                p.cancelled.clear();
                expectEquals(p.addLayer(), i);
                expectEquals((int) p.cancelled.size(), 1, "one staged swap cancelled");
                expectEquals(p.cancelled[0], i, "the new slot's");
            }
            expectEquals(p.getNumChannels(), mu_limits::kMaxLayers);
            expectEquals(p.addLayer(), -1);
            std::array<bool, mu_limits::kMaxLayers> seen {};
            for (int i = 0; i < mu_limits::kMaxLayers; ++i) seen[(size_t) p.getLayer(i)->colourIndex] = true;
            for (bool b : seen) expect(b, "all eight palette colours used once");
            expectEquals(p.destroyed, 0, "nothing was replaced, so nothing was destroyed");
            expectEquals((int) p.moved.size(), mu_limits::kMaxLayers - 1, "one layersMoved per add");
            expect(p.moved.front() == std::make_pair(1, 1) && p.moved.back() == std::make_pair(mu_limits::kMaxLayers - 1, mu_limits::kMaxLayers - 1),
                   "the new slot only");
            for (int h : p.movedHoldingLock) expectEquals(h, 1, "called under layersLock");
        }

        beginTest("addLayer replaces a spare, destroys it outside the lock, and gives the slot default values");
        {
            EditableProcessor p(true);
            auto* spare = p.layers[1].get();
            p.setParam("v1_a", 0.9f);                               // stale values left in the spare slot
            p.setParam("ch1_lvl", 0.4f);
            p.mixerEngine.channels[1].level.store(0.4f);
            expectEquals(p.addLayer(), 1);
            expect(p.layers[1].get() != spare);
            expectEquals(p.destroyed, 1, "the spare is released");
            expectEquals(p.underLock, 0, "after layersLock was dropped");
            expectNear(p.param("v1_a"), 0.25f);
            expectNear(p.param("ch1_lvl"), 1.0f);
            expectNear(p.mixerEngine.channels[1].level.load(), 1.0f);
            expectEquals(p.getNumChannels(), 2);
        }

        beginTest("removeLayer moves the layers above down by pointer, with their parameters and strip state");
        {
            EditableProcessor p(true);
            for (int i = 1; i < 4; ++i) p.addLayer();
            Layer* ptr[4];
            for (int i = 0; i < 4; ++i) { ptr[i] = p.getLayer(i); p.getLayer(i)->colourIndex = i + 3; }
            auto* spareTop = p.layers[4].get();
            p.layers[3]->colourIndex = 6;
            p.destroyed = p.underLock = 0;
            p.cancelled.clear();

            p.setParam("v1_a", 0.1f); p.setParam("v2_a", 0.2f); p.setParam("v3_a", 0.3f);
            p.setParam("ch1_pan", -0.5f); p.setParam("ch2_pan", 0.5f);
            p.mixerEngine.channels[2].level.store(0.7f);
            p.mixerEngine.channels[3].level.store(0.9f);
            // Strip 3 ducks from layer 2 and strip 0 from the layer being removed (APVTS: 0 off, n = layer n-1).
            p.setParam("ch3_scSrc", 3.0f);
            p.setParam("ch0_scSrc", 2.0f);
            p.mixerEngine.channels[3].sidechainSource.store(2);
            p.mixerEngine.channels[0].sidechainSource.store(1);

            expect(p.removeLayer(1));

            expectEquals(p.getNumChannels(), 3);
            expect(p.layers[0].get() == ptr[0]);
            expect(p.layers[1].get() == ptr[2], "layer 2 moved to slot 1 by pointer");
            expect(p.layers[2].get() == ptr[3]);
            expect(p.layers[3] != nullptr && p.layers[3].get() != ptr[1], "the vacated slot is refilled with a spare");
            expect(p.layers[4].get() == spareTop, "spares above the count are untouched");
            expectEquals(p.layers[3]->colourIndex, 6, "the spare keeps the slot's colour");
            expectEquals(p.getLayer(1)->colourIndex, 5, "colour moved with its layer");
            expectEquals(p.probe(1)->movedTo, 1);
            expectEquals(p.destroyed, 1, "only the removed layer is destroyed");
            expectEquals(p.underLock, 0, "and after layersLock was dropped");
            expectEquals((int) p.cancelled.size(), mu_limits::kMaxLayers, "every staged swap cancelled");
            expectEquals((int) p.moved.size(), 3 + 1, "three adds, then the removal");
            expect(p.moved.back() == std::make_pair(1, 3), "the shifted slots and the vacated top one");
            expectEquals(p.movedHoldingLock.back(), 1, "called under layersLock");

            expectNear(p.param("v1_a"), 0.2f); expectNear(p.param("v2_a"), 0.3f); expectNear(p.param("v3_a"), 0.25f);
            expectNear(p.param("ch1_pan"), 0.5f);
            expectNear(p.mixerEngine.channels[1].level.load(), 0.7f);
            expectNear(p.mixerEngine.channels[2].level.load(), 0.9f);
            expectNear(p.mixerEngine.channels[3].level.load(), 1.0f);
            expectEquals(juce::roundToInt(p.param("ch2_scSrc")), 2, "the strip that ducked from layer 2 follows it to slot 1");
            expectEquals(juce::roundToInt(p.param("ch0_scSrc")), 0, "a strip that ducked from the removed layer ducks from nothing");
            expectEquals(p.mixerEngine.channels[2].sidechainSource.load(), 1);
            expectEquals(p.mixerEngine.channels[0].sidechainSource.load(), -1);
        }

        beginTest("removeLayer refuses the last layer and a bad index");
        {
            EditableProcessor p(true);
            expect(! p.removeLayer(0));
            p.addLayer();
            expect(! p.removeLayer(2));
            expect(! p.removeLayer(-1));
            expectEquals(p.getNumChannels(), 2);
        }

        beginTest("removeLayer without spares leaves the vacated slot empty");
        {
            EditableProcessor p(false);
            p.addLayer(); p.addLayer();
            p.destroyed = 0;
            expect(p.removeLayer(0));
            expect(p.layers[2] == nullptr);
            expectEquals(p.getNumChannels(), 2);
            expectEquals(p.destroyed, 1);
            expectEquals(p.underLock, 0);
        }

        beginTest("swapLayers exchanges the layers by pointer, with parameters, strip state and sidechain sources");
        {
            EditableProcessor p(true);
            p.addLayer(); p.addLayer();
            auto* a = p.getLayer(0); auto* c = p.getLayer(2);
            a->colourIndex = 1; c->colourIndex = 6;
            p.destroyed = 0; p.cancelled.clear();
            p.setParam("v0_a", 0.1f); p.setParam("v2_a", 0.8f);
            p.mixerEngine.channels[0].level.store(0.3f);
            p.mixerEngine.channels[2].level.store(0.6f);
            p.setParam("ch1_scSrc", 1.0f);                 // strip 1 ducks from layer 0
            p.mixerEngine.channels[1].sidechainSource.store(0);

            expect(p.swapLayers(0, 2));

            expect(p.getLayer(0) == c);
            expect(p.getLayer(2) == a);
            expectEquals(p.getLayer(0)->colourIndex, 6, "colour moved with its layer");
            expectEquals(p.probe(0)->movedTo, 0);
            expectEquals(p.probe(2)->movedTo, 2);
            expectEquals(p.destroyed, 0, "a swap destroys nothing");
            expectEquals((int) p.cancelled.size(), 2);
            expectEquals((int) p.moved.size(), 2 + 2, "two adds, then one call per exchanged slot");
            expect(p.moved[2] == std::make_pair(0, 0) && p.moved[3] == std::make_pair(2, 2), "not the slot between");
            expectNear(p.param("v0_a"), 0.8f); expectNear(p.param("v2_a"), 0.1f);
            expectNear(p.mixerEngine.channels[0].level.load(), 0.6f);
            expectNear(p.mixerEngine.channels[2].level.load(), 0.3f);
            expectEquals(juce::roundToInt(p.param("ch1_scSrc")), 3, "the strip follows layer 0 to slot 2");
            expectEquals(p.mixerEngine.channels[1].sidechainSource.load(), 2);
            expect(! p.swapLayers(1, 1));
            expect(! p.swapLayers(0, 3));
        }

        beginTest("resetLayer resets in place, keeps name and colour, and clears the slot's values");
        {
            EditableProcessor p(true);
            p.addLayer();
            auto* l = p.getLayer(1);
            l->name = "Keep"; l->colourIndex = 5;
            p.setParam("v1_a", 0.9f); p.setParam("ch1_pan", 0.5f);
            p.mixerEngine.channels[1].level.store(0.2f);
            p.cancelled.clear();
            p.resets = p.destroyed = 0;
            p.moved.clear();

            p.resetLayer(1);

            expect(p.getLayer(1) == l, "reset is in place, so the editor's pointers into the layer stay valid");
            expectEquals(p.resets, 1);
            expectEquals(p.destroyed, 1, "the default layer built before the lock is released afterwards");
            expectEquals(p.underLock, 0, "outside layersLock");
            expectEquals((int) p.moved.size(), 1);
            expect(p.moved[0] == std::make_pair(1, 1));
            expectEquals(p.movedHoldingLock[p.movedHoldingLock.size() - 1], 1, "called under layersLock");
            expectEquals(l->name, std::string("Keep"));
            expectEquals(l->colourIndex, 5);
            expectNear(p.param("v1_a"), 0.25f);
            expectNear(p.param("ch1_pan"), 0.0f);
            expectNear(p.mixerEngine.channels[1].level.load(), 1.0f);
            expectEquals((int) p.cancelled.size(), 1);
            expectEquals(p.cancelled[0], 1);
            p.resetLayer(5);   // out of range: ignored
            expectEquals(p.resets, 1);
            expectEquals((int) p.moved.size(), 1);
        }

        beginTest("structural edits cancel exactly the staged swaps they would misdirect");
        {
            auto stage = [](EditableProcessor& p, std::initializer_list<int> slots, bool full)
            {
                for (int s : slots) p.layerSwapper.stageSlot(s, juce::ValueTree("Layer"));
                if (full) p.layerSwapper.stageFull(juce::ValueTree("Full"));
            };

            EditableProcessor add(true);
            stage(add, { 0, 1 }, false);
            add.addLayer();   // the new slot is 1
            expect(add.hasPendingSwap(0), "an existing layer's swap is kept");
            expect(! add.hasPendingSwap(1), "the new slot carries no stale swap");

            EditableProcessor rem(true);
            rem.addLayer(); rem.addLayer();
            rem.layerSwapper.stageFull(juce::ValueTree("Full"));
            stage(rem, { 0, 2 }, false);        // slot swaps staged after the full preset
            rem.removeLayer(1);   // the down-shift renumbers slot 2
            expect(! rem.hasPendingSwap(0) && ! rem.hasPendingSwap(2), "removal cancels every per-layer swap");
            expect(rem.hasPendingFullPreset(), "a staged full preset is left to win");

            EditableProcessor swp(true);
            swp.addLayer(); swp.addLayer();
            stage(swp, { 0, 1, 2 }, false);
            swp.swapLayers(0, 2);
            expect(! swp.hasPendingSwap(0) && ! swp.hasPendingSwap(2), "both swapped slots are cancelled");
            expect(swp.hasPendingSwap(1), "the slot between is untouched");

            EditableProcessor rst(true);
            rst.addLayer();
            rst.layerSwapper.stageFull(juce::ValueTree("Full"));
            stage(rst, { 0, 1 }, false);        // slot swaps staged after the full preset
            rst.resetLayer(1);
            expect(rst.hasPendingSwap(0), "another layer's swap is kept");
            expect(! rst.hasPendingSwap(1), "the reset slot's swap would re-fill it, so it is cancelled");
            expect(rst.hasPendingFullPreset(), "the full preset is left to win");
        }
    }
};

static LayerOwnershipTests layerOwnershipTests;
