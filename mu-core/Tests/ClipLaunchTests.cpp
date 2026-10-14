#include "Plugin/ProcessorBase.h"
#include "Plugin/MixerFxParams.h"

#include <vector>

// Clip store / launch on ProcessorBase through a small test backend whose layer preset is a
// "TestLayer" node (the "v{N}_" parameter and the layer's modulators), staged like a real product's.
namespace
{
    struct ClipProcessor : ProcessorBase
    {
        ClipProcessor()
            : ProcessorBase(BusesProperties().withOutput("Out", juce::AudioChannelSet::stereo()), makeLayout())
        {
            juce::StringArray prefixes;
            for (int i = 0; i < mu_limits::kMaxLayers; ++i) prefixes.add("v" + juce::String(i) + "_");
            initLayerState(prefixes);
            layers[0] = createLayer(0);
            numLayers.store(1);
            layerSwapper.setAppliers([](juce::ValueTree&) {},
                                     [this](int slot, juce::ValueTree& t)
                                     {
                                         applyLayerNode(slot, t);
                                         ++commits;
                                         if (onLayerPresetCommitted) onLayerPresetCommitted(slot);
                                     });
            addLayer();
            addLayer();
        }

        ~ClipProcessor() override { for (auto& l : layers) l.reset(); }

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

        using ProcessorBase::layerSwapper;
        using ProcessorCore::layers;
        using ProcessorBase::captureComposedState;
        bool canEditLayers() const override { return true; }
        std::unique_ptr<Layer> createLayer(int) override { return std::make_unique<Layer>(); }

        // The product-side split: capture builds the file's tree, stage is everything after the parse.
        juce::ValueTree captureLayerPresetTree(int slot) override
        {
            return slot >= 0 && slot < getNumLayers() ? captureLayerNode(slot, "TestLayer") : juce::ValueTree();
        }
        void stageLayerPresetTree(int slot, juce::ValueTree tree, bool keepIdentity) override
        {
            ++stages;
            lastKeepIdentity = keepIdentity;
            layerSwapper.useSlot(slot, std::move(tree));
        }
        void loadLayerPreset(int slot, const juce::File& file) override
        {
            auto xml = juce::XmlDocument::parse(file);
            if (xml == nullptr || ! xml->hasTagName("TestLayer")) { if (onLoadError) onLoadError("bad"); return; }
            stageLayerPreset(slot, juce::ValueTree::fromXml(*xml), false, false);
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
        // Pretend the transport runs (a block was seen playing), so loads stage instead of applying.
        void startPlaying()  { layerSwapper.flagBoundaries(true, 0.0, 0.1); }
        // Stage a plain (non-clip) layer-preset load from `tree`.
        void stageNonClip(int slot, juce::ValueTree tree) { stageLayerPreset(slot, std::move(tree), false, false); }

        juce::String getLayerName(int) const override { return {}; }
        int getLayerColourIndex(int i) const override { return layers[(size_t) i] ? layers[(size_t) i]->colourIndex : 0; }
        juce::File   getLayerPresetDir()       const override { return {}; }
        juce::String getLayerPresetExtension() const override { return ".muTest"; }
        juce::String getFullPresetExtension()  const override { return ".muTest"; }
        void applyFullMidiPreset(const juce::File&) override {}
        const juce::String getName() const override          { return "clip-test"; }
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

        int stages = 0, commits = 0;
        bool lastKeepIdentity = false;
    };

    juce::File writeTree(const juce::File& dir, const juce::String& name, const juce::ValueTree& t)
    {
        auto f = dir.getChildFile(name);
        if (auto xml = t.createXml())
            xml->writeTo(f);
        return f;
    }
}

struct ClipLaunchTests : juce::UnitTest
{
    ClipLaunchTests() : juce::UnitTest("ProcessorBase clip launch", "mu-core") {}

    void runTest() override
    {
        beginTest("storeClip then launchClip (stopped) restores the layer, leaves the strip and identity alone, and never writes the bank");
        {
            ClipProcessor p;
            p.layers[0]->name = "keep"; p.layers[0]->colourIndex = 3;
            p.setParam("v0_a", 0.9f);
            expect(p.storeClip(0, 2, "loud"));
            expectEquals((int) p.clipMask(0), 1 << 2);
            expectEquals(p.layers[0]->clips->getName(2), juce::String("loud"));

            p.setParam("v0_a", 0.1f);
            p.setParam("ch0_pan", 0.5f);
            const auto before = p.layers[0]->clips->get(2);
            expect(p.launchClip(0, 2));
            expectWithinAbsoluteError(p.param("v0_a"), 0.9f, 0.01f);
            expectWithinAbsoluteError(p.param("ch0_pan"), 0.5f, 0.001f);
            expect(p.layers[0]->name == "keep" && p.layers[0]->colourIndex == 3, "name and colour untouched");
            expect(p.layers[0]->clips->get(2).isEquivalentTo(before), "bank unchanged by the launch");
            expectEquals((int) p.clipMask(0), 1 << 2);
            expect(p.lastKeepIdentity, "a launch keeps the layer's identity");
        }

        beginTest("launchClip returns false and changes nothing for an empty clip, bad indices, or a staged full preset");
        {
            ClipProcessor p;
            p.storeClip(0, 0);
            p.setParam("v0_a", 0.6f);
            expect(! p.launchClip(0, 1), "empty clip");
            expect(! p.launchClip(0, -1) && ! p.launchClip(0, 8), "clip out of range");
            expect(! p.launchClip(-1, 0) && ! p.launchClip(7, 0), "layer out of range");
            expect(! p.storeClip(0, 8) && ! p.storeClip(5, 0), "store out of range");
            p.layerSwapper.stageFull(juce::ValueTree("Full"));
            expect(! p.launchClip(0, 0), "a full preset is staged");
            expectEquals(p.stages, 0);
            expectWithinAbsoluteError(p.param("v0_a"), 0.6f, 0.01f);
        }

        beginTest("launch while playing is pending and applies only at the boundary, firing onLayerPresetCommitted");
        {
            ClipProcessor p;
            p.setParam("v0_a", 0.8f);
            p.storeClip(0, 0);
            p.setParam("v0_a", 0.2f);
            int committed = -1;
            p.onLayerPresetCommitted = [&committed](int s) { committed = s; };

            p.startPlaying();
            expect(p.launchClip(0, 0));
            expect(p.hasPendingSwap(0), "staged, not applied");
            expectWithinAbsoluteError(p.param("v0_a"), 0.2f, 0.01f);

            expect(p.layerSwapper.flagBoundaries(true, 3.95, 0.2), "the bar line flags it");
            p.layerSwapper.commit();
            expect(! p.hasPendingSwap(0));
            expectWithinAbsoluteError(p.param("v0_a"), 0.8f, 0.01f);
            expectEquals(committed, 0);
        }

        beginTest("storing right after a playing preset load keeps that preset in the clip and lets it land");
        {
            ClipProcessor src;
            src.setParam("v0_a", 0.9f);
            const auto presetA = src.captureLayerPresetTree(0);
            src.setParam("v0_a", 0.6f);
            const auto presetB = src.captureLayerPresetTree(0);

            ClipProcessor p;
            p.setParam("v0_a", 0.1f);
            p.startPlaying();
            p.stageNonClip(0, presetA);
            expect(p.storeClip(0, 0));
            p.stageNonClip(0, presetB);
            expect(p.storeClip(0, 1));
            expect(p.hasPendingSwap(0), "the store left the chosen preset staged");
            expect(! p.layers[0]->clips->get(0).isEquivalentTo(p.layers[0]->clips->get(1)), "the clips differ");

            expect(p.layerSwapper.flagBoundaries(true, 3.95, 0.2));
            p.layerSwapper.commit();
            expectWithinAbsoluteError(p.param("v0_a"), 0.6f, 0.01f);
            expect(p.launchClip(0, 0));
            expect(p.layerSwapper.flagBoundaries(true, 7.95, 0.2));
            p.layerSwapper.commit();
            expectWithinAbsoluteError(p.param("v0_a"), 0.9f, 0.01f);
        }

        beginTest("a launch equals loading the same layer preset file directly");
        {
            auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("mu-clip-launch-test");
            dir.createDirectory();

            ClipProcessor src;
            src.setParam("v0_a", 0.7f);
            const auto file = writeTree(dir, "x.muTest", src.captureLayerPresetTree(0));

            ClipProcessor viaClip, viaLoad;
            expect(viaClip.loadClipFromFile(0, 5, file));
            expect(viaClip.launchClip(0, 5));
            viaLoad.loadLayerPreset(0, file);

            expect(viaClip.captureLayerPresetTree(0).isEquivalentTo(viaLoad.captureLayerPresetTree(0)));
            expectWithinAbsoluteError(viaClip.param("v0_a"), 0.7f, 0.01f);
            dir.deleteRecursively();
        }

        beginTest("loadClipFromFile rejects a wrong tag and an unreadable file, raising onLoadError and leaving the bank unchanged");
        {
            auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("mu-clip-launch-test2");
            dir.createDirectory();

            ClipProcessor p;
            p.storeClip(0, 1, "kept");
            juce::StringArray errors;
            p.onLoadError = [&errors](const juce::String& e) { errors.add(e); };

            const auto wrong = writeTree(dir, "wrong.muTest", juce::ValueTree("OtherProduct"));
            expect(! p.loadClipFromFile(0, 1, wrong));
            expect(! p.loadClipFromFile(0, 1, dir.getChildFile("missing.muTest")));
            const auto junk = dir.getChildFile("junk.muTest");
            junk.replaceWithText("not xml at all");
            expect(! p.loadClipFromFile(0, 1, junk));
            expectEquals(errors.size(), 3);
            expectEquals((int) p.clipMask(0), 1 << 1);
            expectEquals(p.layers[0]->clips->getName(1), juce::String("kept"));
            expectEquals(p.layers[0]->clips->get(1).getType().toString(), juce::String("TestLayer"));
            dir.deleteRecursively();
        }

        beginTest("a non-clip layer-preset load clears activeClip; a launch does not");
        {
            auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("mu-clip-launch-test3");
            dir.createDirectory();

            ClipProcessor p;
            const auto file = writeTree(dir, "x.muTest", p.captureLayerPresetTree(0));
            p.storeClip(0, 4);
            p.layers[0]->activeClip = 4;
            expect(p.launchClip(0, 4));
            expectEquals(p.layers[0]->activeClip, 4, "the launch leaves it for the live state to set");
            p.loadLayerPreset(0, file);
            expectEquals(p.layers[0]->activeClip, -1);
            expectEquals((int) p.clipMask(0), 1 << 4, "the load leaves the bank alone");
            dir.deleteRecursively();
        }

        beginTest("clearClip empties one clip, drops it as the active clip, and the mask follows");
        {
            ClipProcessor p;
            p.storeClip(0, 0); p.storeClip(0, 3); p.storeClip(1, 3);
            p.layers[0]->activeClip = 3;
            expectEquals((int) p.clipMask(0), 0x09);
            expect(p.clearClip(0, 3));
            expectEquals((int) p.clipMask(0), 0x01);
            expectEquals(p.layers[0]->activeClip, -1);
            expectEquals((int) p.clipMask(1), 0x08);
            expect(! p.clearClip(0, 8) && ! p.clearClip(9, 0));
            expectEquals((int) p.clipMask(9), 0);
        }

        beginTest("the screen's StoreClip / ClearClip / LaunchClip actions: store plays the stored clip, a launch works with the mode off");
        {
            using T = mu_core::ControlActionType;
            const auto act = [](T type, int layer, int index)
            {
                mu_core::ControlAction a;
                a.type = type; a.layer = layer; a.index = index;
                return a;
            };
            ClipProcessor p;
            p.setParam("v0_a", 0.9f);
            expect(p.perform(act(T::StoreClip, 0, 2)), "store");
            expectEquals((int) p.clipMask(0), 1 << 2);
            expectEquals(p.clipCount(0), 1);
            expectEquals(p.getLiveState().getPlayingClip(0), 2, "what was stored is what plays");
            expectEquals(p.layers[0]->activeClip, 2, "and is recorded with the layer");
            expect(! p.perform(act(T::StoreClip, 0, 8)) && ! p.perform(act(T::StoreClip, 0, -1)), "clip out of range");
            expect(! p.perform(act(T::StoreClip, 7, 0)) && ! p.perform(act(T::ClearClip, 7, 0)), "no such layer");

            p.setParam("v0_a", 0.1f);
            expect(p.perform(act(T::StoreClip, 0, 4)), "a second clip");
            expectEquals(p.getLiveState().getPlayingClip(0), 4);
            expectEquals(p.clipCount(0), 2);
            expect(p.perform(act(T::LaunchClip, 0, 2)), "a launch needs no Performance mode");
            expectWithinAbsoluteError(p.param("v0_a"), 0.9f, 0.01f);
            expectEquals(p.getLiveState().getPlayingClip(0), 2);

            expect(p.perform(act(T::ClearClip, 0, 2)), "clear");
            expectEquals((int) p.clipMask(0), 1 << 4);
            expectEquals(p.getLiveState().getPlayingClip(0), -1, "clearing the playing clip leaves none playing");

            expect(mu_core::actionFromName(mu_core::actionName(T::StoreClip)) == T::None
                   && mu_core::actionFromName(mu_core::actionName(T::ClearClip)) == T::None,
                   "no saved map or surface mapping can name the writing actions");
        }

        beginTest("storing over a clip that is queued drops that launch; Performance mode refuses the writing actions");
        {
            using T = mu_core::ControlActionType;
            const auto act = [](T type, int layer, int index)
            {
                mu_core::ControlAction a;
                a.type = type; a.layer = layer; a.index = index;
                return a;
            };
            const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                 .getChildFile("mu_clipstrip_" + juce::String(juce::Time::currentTimeMillis()));
            dir.createDirectory();

            juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Standalone);
            ClipProcessor p;
            juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Undefined);

            p.setParam("v0_a", 0.9f);  p.storeClip(0, 1);
            p.setParam("v0_a", 0.2f);
            p.startPlaying();
            expect(p.perform(act(T::LaunchClip, 0, 1)), "launch while the transport runs");
            expectEquals(p.getLiveState().getQueuedClip(0), 1);
            expect(p.perform(act(T::StoreClip, 0, 1)), "store over the queued clip");
            expectEquals(p.getLiveState().getQueuedClip(0), -1, "the stale launch is gone");
            expect(! p.hasPendingSwap(0), "and nothing is staged");
            expectEquals(p.getLiveState().getPlayingClip(0), 1);

            mu_pp::PerformanceFile list;
            list.setItem(0, p.getProductStateId(), dir.getChildFile("p.preset"), dir, "P");
            p.loadPerformance(list, dir.getChildFile("live.muPerformance"));
            expect(p.perform(act(T::PerformanceMode, 0, 1)), "Performance mode on");
            expect(! p.perform(act(T::StoreClip, 0, 3)) && ! p.perform(act(T::ClearClip, 0, 1)), "locked: the screen-only writes are refused");
            expectEquals((int) p.clipMask(0), 1 << 1, "the bank is unchanged");
            dir.deleteRecursively();
        }

        beginTest("a store keeps a staged preset and holds it, and is refused while a full preset is staged");
        {
            using T = mu_core::ControlActionType;
            const auto act = [](T type, int layer, int index)
            {
                mu_core::ControlAction a;
                a.type = type; a.layer = layer; a.index = index;
                return a;
            };

            // A staged non-clip layer preset is what the store captures, and it still lands: the clip, the layer
            // and the lit pad all agree.
            {
                ClipProcessor p;
                p.setParam("v0_a", 0.3f);
                const auto older = p.captureLayerPresetTree(0);
                p.setParam("v0_a", 0.7f);
                p.startPlaying();
                p.stageNonClip(0, older);
                expect(p.hasPendingSwap(0), "the plain preset is staged");
                expect(p.perform(act(T::StoreClip, 0, 3)), "store");
                expect(p.hasPendingSwap(0), "the staged preset is kept");
                expect(p.layers[0]->clips->get(3).isEquivalentTo(older), "the clip holds the staged preset, not the live layer");
                p.layerSwapper.flagBoundaries(true, 3.95, 0.2);
                p.layerSwapper.commit();
                expectWithinAbsoluteError(p.param("v0_a"), 0.3f, 0.01f);
                expectEquals(p.getLiveState().getPlayingClip(0), 3, "the stored pad is the playing one");
                expectEquals(p.getLiveState().getQueuedClip(0), -1);
                expect(p.layers[0]->clips->isOccupied(3), "and the clip is stored");
            }

            // A staged full preset refuses both writes and leaves the bank alone.
            {
                ClipProcessor p;
                p.storeClip(0, 1);
                p.layerSwapper.stageFull(juce::ValueTree("Full"));
                expect(! p.perform(act(T::StoreClip, 0, 2)) && ! p.perform(act(T::ClearClip, 0, 1)), "refused");
                expectEquals((int) p.clipMask(0), 1 << 1, "the bank is unchanged");
            }

            // Another clip queued: storing into a different pad drops that launch and lights the stored pad.
            {
                ClipProcessor p;
                p.setParam("v0_a", 0.9f);  p.storeClip(0, 1);
                p.setParam("v0_a", 0.2f);
                p.startPlaying();
                expect(p.perform(act(T::LaunchClip, 0, 1)), "launch A");
                expectEquals(p.getLiveState().getQueuedClip(0), 1);
                expect(p.perform(act(T::StoreClip, 0, 2)), "store B");
                expectEquals(p.getLiveState().getQueuedClip(0), -1, "A's queue is cancelled");
                expectEquals(p.getLiveState().getPlayingClip(0), 2, "B is lit");
                expect(! p.hasPendingSwap(0));
                expectWithinAbsoluteError(p.param("v0_a"), 0.2f, 0.01f);
            }

            // A mapped LaunchClip needs no Performance mode.
            {
                ClipProcessor p;
                p.setParam("v0_a", 0.8f);  p.storeClip(0, 0);
                p.setParam("v0_a", 0.1f);
                expect(! p.getLiveState().isPerformanceMode());
                expect(p.perform(act(T::LaunchClip, 0, 0)), "launch with the mode off");
                expectWithinAbsoluteError(p.param("v0_a"), 0.8f, 0.01f);
            }
        }

        beginTest("a clip survives the layer's own parameter edits and is independent of the live layer");
        {
            ClipProcessor p;
            p.setParam("v0_a", 0.5f);
            p.storeClip(0, 0);
            const auto stored = p.layers[0]->clips->get(0);
            p.setParam("v0_a", 0.9f);
            expect(p.layers[0]->clips->get(0).isEquivalentTo(stored), "the stored tree does not alias the live layer");
        }

        beginTest("MuteSelectedLayer flips the selected layer's strip mute; AppPrev and AppNext are refused");
        {
            using T = mu_core::ControlActionType;
            mu_core::ControlAction mute; mute.type = T::MuteSelectedLayer;
            ClipProcessor p;
            expectEquals(p.getLiveState().getSelectedLayer(), 0);
            expect(! p.getLiveState().isLayerMuted(0));
            expect(p.perform(mute), "mute");
            expect(p.getLiveState().isLayerMuted(0), "the live view sees the strip mute");
            expect(p.perform(mute), "and unmute");
            expect(! p.getLiveState().isLayerMuted(0));
            mu_core::ControlAction up; up.type = T::AppPrev;
            mu_core::ControlAction down; down.type = T::AppNext;
            expect(! p.perform(up) && ! p.perform(down), "no multi-app hub: refused");
        }

        beginTest("TransportToggle plays and stops the internal clock, and the live view reports it");
        {
            using T = mu_core::ControlActionType;
            mu_core::ControlAction toggle; toggle.type = T::TransportToggle;
            ClipProcessor p;
            const mu_core::LiveView& view = *p.getLiveView();
            expect(! view.isTransportPlaying() && ! view.isTransportOwnedOutside());
            expect(p.perform(toggle));
            expect(view.isTransportPlaying(), "playing");
            expect(p.perform(toggle));
            expect(! view.isTransportPlaying(), "stopped again");
        }
    }
};

static ClipLaunchTests clipLaunchTests;
