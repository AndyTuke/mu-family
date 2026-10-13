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
        using ProcessorBase::captureComposedState;
        using ProcessorBase::applyComposedState;
        using ProcessorBase::savedLayerCount;
        using ProcessorBase::unsavedLayerCount;
        bool editable = true;
        bool canEditLayers() const override { return editable; }

        // A fake product's layer references (like mu-On's Rumble source): refs[k] names a layer.
        std::vector<int> refs;
        void remapLayerReferences(const std::function<int(int)>& oldToNew) override
        {
            ++remapCalls;
            for (auto& r : refs) r = oldToNew(r);
        }
        int remapCalls = 0;

        std::unique_ptr<Layer> createLayer(int) override
        {
            return std::make_unique<ProbeLayer>(layersLock, destroyed, underLock, resets);
        }
        void cancelPendingSwap(int slot) override { cancelled.push_back(slot); ProcessorBase::cancelPendingSwap(slot); }
        void layersMoved(int first, int last) override
        {
            moved.push_back({ first, last });
            movedHoldingLock.push_back(lockHeldByOtherThread() ? 1 : 0);
            order.push_back("moved");
        }

        // A product whose layer data is the parameters' truth (mu-Clid's Rhythm): the edits hand it the slots
        // to push instead of copying values in the tree. layersReordered tells it where data came from.
        bool truth = false;
        bool layerHoldsParamTruth() const override { return truth; }
        void syncSlotParams(int slot) override
        {
            synced.push_back(slot);
            syncedFlagUp.push_back(layerEditInFlight.load() ? 1 : 0);
            syncedSuspended.push_back(isSuspended() ? 1 : 0);
            syncedHoldingLock.push_back(lockHeldByOtherThread() ? 1 : 0);
            order.push_back("synced");
        }
        struct Reorder { LayerEdit kind; int a, b; };
        void layersReordered(LayerEdit kind, int a, int b) override
        {
            reordered.push_back({ kind, a, b });
            reorderedHoldingLock.push_back(lockHeldByOtherThread() ? 1 : 0);
            order.push_back("reordered");
        }
        std::vector<int> synced, syncedHoldingLock, reorderedHoldingLock, syncedFlagUp, syncedSuspended, addedFlagUp;
        std::vector<Reorder> reordered;
        std::vector<juce::String> order;          // layersReordered / layersMoved / syncSlotParams, in call order
        void clearLogs() { syncedFlagUp.clear(); syncedSuspended.clear(); addedFlagUp.clear(); synced.clear(); syncedHoldingLock.clear(); reordered.clear(); reorderedHoldingLock.clear(); order.clear(); }
        // A critical section is re-entrant for its owner, so only a second thread can tell it is held.
        bool lockHeldByOtherThread()
        {
            bool acquired = false;
            std::thread probeThread([&] { acquired = layersLock.tryEnter(); if (acquired) layersLock.exit(); });
            probeThread.join();
            return ! acquired;
        }

        // layerAdded(index): the new layer's own starting point, after its parameters are at defaults.
        void layerAdded(int index) override { added.push_back(index); addedFlagUp.push_back(layerEditInFlight.load() ? 1 : 0); addedHoldingLock.push_back(lockHeldByOtherThread() ? 1 : 0); }

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
        std::vector<int> added, addedHoldingLock; // layerAdded(index) calls; 1 when layersLock was held

        juce::String getLayerName(int) const override { return {}; }
        int getLayerColourIndex(int i) const override { return layers[(size_t) i] ? layers[(size_t) i]->colourIndex : 0; }
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
            expectEquals(p.getNumLayers(), 3);
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
        runParamTruthTests();
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
            expectEquals(p.getNumLayers(), 3);
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
            expectEquals(p.getNumLayers(), 3);
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
            expectEquals(p.getNumLayers(), 2);
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
            expectEquals(p.getNumLayers(), 3);
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
            expect(! p.canReorderLayers());   // the sidebar offers no drag-reorder without layer editing
        }

        beginTest("a product with editable layers lets the sidebar reorder by default");
        {
            EditableProcessor p(false);
            expect(p.canReorderLayers());
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
            expectEquals(p.getNumLayers(), mu_limits::kMaxLayers);
            expectEquals(p.addLayer(), -1);
            std::array<bool, mu_limits::kMaxLayers> seen {};
            for (int i = 0; i < mu_limits::kMaxLayers; ++i) seen[(size_t) p.getLayer(i)->colourIndex] = true;
            for (bool b : seen) expect(b, "all eight palette colours used once");
            expectEquals(p.destroyed, 0, "nothing was replaced, so nothing was destroyed");
            expectEquals((int) p.moved.size(), mu_limits::kMaxLayers - 1, "one layersMoved per add");
            expect(p.moved.front() == std::make_pair(1, 1) && p.moved.back() == std::make_pair(mu_limits::kMaxLayers - 1, mu_limits::kMaxLayers - 1),
                   "the new slot only");
            for (int h : p.movedHoldingLock) expectEquals(h, 1, "called under layersLock");
            expectEquals((int) p.added.size(), mu_limits::kMaxLayers - 1, "one layerAdded per add");
            expectEquals(p.added.front(), 1);
            expectEquals(p.added.back(), mu_limits::kMaxLayers - 1);
            for (int h : p.addedHoldingLock) expectEquals(h, 0, "layerAdded runs outside layersLock");
            p.removeLayer(0); p.swapLayers(0, 1); p.resetLayer(0);
            expectEquals((int) p.added.size(), mu_limits::kMaxLayers - 1, "delete, reorder and reset do not call layerAdded");
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
            expectEquals(p.getNumLayers(), 2);
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

            expectEquals(p.getNumLayers(), 3);
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

        beginTest("a ducked strip keeps its sidechain follower when a remove or a swap moves it");
        {
            EditableProcessor p(true);
            for (int i = 1; i < 4; ++i) p.addLayer();
            for (int i = 0; i < 4; ++i) p.mixerEngine.setSidechainEnv(i, 0.1f * (float) (i + 1));
            expect(p.removeLayer(1));
            expectNear(p.mixerEngine.getSidechainEnv(0), 0.1f);
            expectNear(p.mixerEngine.getSidechainEnv(1), 0.3f);
            expectNear(p.mixerEngine.getSidechainEnv(2), 0.4f);
            expectNear(p.mixerEngine.getSidechainEnv(3), 0.0f);
            expect(p.swapLayers(0, 2));
            expectNear(p.mixerEngine.getSidechainEnv(0), 0.4f);
            expectNear(p.mixerEngine.getSidechainEnv(2), 0.1f);
            expectNear(p.mixerEngine.getSidechainEnv(1), 0.3f);
        }

        beginTest("removeLayer refuses the last layer and a bad index");
        {
            EditableProcessor p(true);
            expect(! p.removeLayer(0));
            p.addLayer();
            expect(! p.removeLayer(2));
            expect(! p.removeLayer(-1));
            expectEquals(p.getNumLayers(), 2);
        }

        beginTest("removeLayer without spares leaves the vacated slot empty");
        {
            EditableProcessor p(false);
            p.addLayer(); p.addLayer();
            p.destroyed = 0;
            expect(p.removeLayer(0));
            expect(p.layers[2] == nullptr);
            expectEquals(p.getNumLayers(), 2);
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

        beginTest("layer references follow their layer through removeLayer and swapLayers; a gone layer maps to -1");
        {
            EditableProcessor p(true);
            p.addLayer(); p.addLayer();   // three layers
            p.refs = { 0, 1, 2 };         // reference k names layer k
            expect(p.removeLayer(1));
            expectEquals(p.remapCalls, 1);
            expectEquals(p.refs[0], 0, "a reference below the removed layer is unchanged");
            expectEquals(p.refs[1], -1, "a reference to the removed layer maps to -1");
            expectEquals(p.refs[2], 1, "a reference above the removed layer follows it down");

            p.refs = { 0, 1 };
            expect(p.swapLayers(0, 1));
            expectEquals(p.remapCalls, 2);
            expectEquals(p.refs[0], 1, "a reference follows its layer to the swapped slot");
            expectEquals(p.refs[1], 0);

            p.addLayer(); p.resetLayer(0);
            expectEquals(p.remapCalls, 2, "addLayer and resetLayer do not remap references");
        }

        beginTest("the layer count and colours save with the composed state and a missing count uses unsavedLayerCount");
        {
            EditableProcessor src(true);
            src.addLayer(); src.addLayer();
            src.layers[1]->colourIndex = 5;
            auto state = src.captureComposedState();
            expectEquals((int) state.getProperty("numVoices"), 3);
            expect(state.getProperty("voiceColours").toString().startsWith("0,5,"), "colours are saved per slot");

            EditableProcessor dst(true);
            expectEquals(dst.savedLayerCount(state), 3);
            dst.applyComposedState(state);
            expectEquals(dst.getNumLayers(), 3, "the count is restored");
            expectEquals(dst.layers[1]->colourIndex, 5, "the colours are restored");

            state.removeProperty("numVoices", nullptr);
            state.removeProperty("voiceColours", nullptr);
            expectEquals(dst.savedLayerCount(state), 1, "unsavedLayerCount defaults to 1");
            dst.unsavedLayerCount = 4;
            dst.applyComposedState(state);
            expectEquals(dst.getNumLayers(), 4, "a state without a count loads with the product's legacy count");
            expectEquals(dst.layers[1]->colourIndex, 5, "a state without colours keeps the current ones");

            state.setProperty("numVoices", 99, nullptr);
            expectEquals(dst.savedLayerCount(state), mu_limits::kMaxLayers, "the count is clamped");
            state.setProperty("numVoices", 0, nullptr);
            expectEquals(dst.savedLayerCount(state), 1);

            EditableProcessor fixed(false);
            fixed.editable = false;
            auto fixedState = fixed.captureComposedState();
            expect(! fixedState.hasProperty("numVoices") && ! fixedState.hasProperty("voiceColours"),
                   "a fixed-count product saves neither");
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

    // layersReordered and the param-truth hooks (layerHoldsParamTruth / syncSlotParams), driven by a fake product.
    void runParamTruthTests()
    {
        beginTest("layersReordered fires once per remove or swap, under the lock, before layersMoved");
        {
            EditableProcessor p(true);
            p.addLayer(); p.addLayer(); p.addLayer();
            p.clearLogs(); p.moved.clear();

            expect(p.removeLayer(1));
            expectEquals((int) p.reordered.size(), 1);
            expect(p.reordered[0].kind == ProcessorBase::LayerEdit::Remove && p.reordered[0].a == 1 && p.reordered[0].b == -1);
            expectEquals(p.reorderedHoldingLock[0], 1, "called under layersLock");
            expect(p.order.size() >= 2 && p.order[0] == "reordered" && p.order[1] == "moved", "before layersMoved");
            expect(p.synced.empty(), "a product whose parameters are the truth is not asked to sync");

            p.clearLogs();
            expect(p.swapLayers(0, 2));
            expectEquals((int) p.reordered.size(), 1, "one call for the swap, not one per slot");
            expect(p.reordered[0].kind == ProcessorBase::LayerEdit::Swap && p.reordered[0].a == 0 && p.reordered[0].b == 2);
            expectEquals(p.reorderedHoldingLock[0], 1);
            expect(p.order.size() == 3 && p.order[0] == "reordered" && p.order[1] == "moved" && p.order[2] == "moved",
                   "before both layersMoved calls");

            p.clearLogs();
            p.resetLayer(1); p.addLayer();
            expect(p.reordered.empty(), "reset and add reorder nothing");
        }

        beginTest("layerHoldsParamTruth: removeLayer syncs the shifted and the vacated slots instead of copying values");
        {
            EditableProcessor p(true);
            p.truth = true;
            p.addLayer(); p.addLayer(); p.addLayer();
            p.setParam("v1_a", 0.1f); p.setParam("v2_a", 0.2f); p.setParam("v3_a", 0.3f);
            p.setParam("ch2_pan", 0.5f);
            p.clearLogs();

            expect(p.removeLayer(1));
            expect((p.synced == std::vector<int> { 1, 2, 3 }), "the removed index and every slot above it, the vacated top one included");
            for (int h : p.syncedHoldingLock) expectEquals(h, 0, "outside layersLock");
            expect(p.order.back() == "synced", "after the pointer moves");
            expectNear(p.param("v1_a"), 0.1f); expectNear(p.param("v2_a"), 0.2f); expectNear(p.param("v3_a"), 0.3f);
            expectNear(p.param("ch2_pan"), 0.5f);   // the product's sync moves values; the base leaves the tree alone
            expectEquals(p.layerEditInFlight.load() ? 1 : 0, 0, "the edit flag is down again");
            for (int f : p.syncedFlagUp)    expectEquals(f, 0, "the sync runs with the in-flight flag already down, so the mix is not silenced");
            for (int s : p.syncedSuspended) expectEquals(s, 0, "and with processing already resumed");
        }

        beginTest("layerHoldsParamTruth: swap, reset and add also push with the flag down; layerAdded runs outside the flag");
        {
            EditableProcessor p(true);
            p.truth = true;
            p.addLayer(); p.addLayer();
            p.clearLogs();
            p.swapLayers(0, 1); p.resetLayer(1); p.addLayer();
            expect(p.syncedFlagUp.size() == 4);
            for (int f : p.syncedFlagUp)    expectEquals(f, 0);
            for (int s : p.syncedSuspended) expectEquals(s, 0);
            expect(p.addedFlagUp.size() == 1 && p.addedFlagUp[0] == 0, "layerAdded (file work in mu-Clid) is outside the flag");
        }

        beginTest("without layerHoldsParamTruth the flag stays up through layerAdded");
        {
            EditableProcessor p(true);
            p.addLayer();
            p.clearLogs();
            p.addLayer();
            expect(p.addedFlagUp.size() == 1 && p.addedFlagUp[0] == 1, "products whose audio reads the parameters keep the flag through the edit");
        }

        beginTest("layerHoldsParamTruth: swapLayers, resetLayer and addLayer sync exactly the touched slots");
        {
            EditableProcessor p(true);
            p.truth = true;
            p.addLayer(); p.addLayer();
            p.setParam("v0_a", 0.8f); p.setParam("v2_a", 0.9f);
            p.clearLogs();

            expect(p.swapLayers(0, 2));
            expect((p.synced == std::vector<int> { 0, 2 }), "both exchanged slots and not the one between");
            expectNear(p.param("v0_a"), 0.8f); expectNear(p.param("v2_a"), 0.9f);

            p.clearLogs();
            p.resetLayer(1);
            expect((p.synced == std::vector<int> { 1 }));

            p.clearLogs();
            p.setParam("v3_a", 0.7f);                 // a stale value in the spare slot the add is about to use
            expectEquals(p.addLayer(), 3);
            expect((p.synced == std::vector<int> { 3 }), "the new slot");
            expectNear(p.param("v3_a"), 0.7f);
            expect(p.order.back() == "synced", "the sync is the last hook before layerAdded");
            for (int h : p.syncedHoldingLock) expectEquals(h, 0);
        }

        beginTest("without layerHoldsParamTruth the edits move the values themselves and never call syncSlotParams");
        {
            EditableProcessor p(true);
            p.addLayer(); p.addLayer();
            p.setParam("v1_a", 0.4f); p.setParam("v2_a", 0.6f);
            p.clearLogs();
            expect(p.removeLayer(1));
            expect(p.synced.empty());
            expectNear(p.param("v1_a"), 0.6f);
            expectNear(p.param("v2_a"), 0.25f);
            expect(p.swapLayers(0, 1));
            p.resetLayer(0);
            expect(p.synced.empty());
        }
    }
};

static LayerOwnershipTests layerOwnershipTests;
