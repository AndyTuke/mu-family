#include "Plugin/ProcessorBase.h"
#include "Plugin/MixerFxParams.h"
#include "Sequencer/ClipClipboard.h"

// Clip auto-save on ProcessorBase through a small test backend whose layer preset is a "TestLayer" node, staged
// like a real product's. Each case is its own member function (the cases build several processors).
namespace
{
    struct AutoSaveProcessor : ProcessorBase
    {
        AutoSaveProcessor()
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
                                         if (onLayerPresetCommitted) onLayerPresetCommitted(slot);
                                     });
            addLayer();
            addLayer();
        }

        ~AutoSaveProcessor() override { for (auto& l : layers) l.reset(); }

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

        juce::ValueTree captureLayerPresetTree(int slot) override
        {
            return slot >= 0 && slot < getNumLayers() ? captureLayerNode(slot, "TestLayer") : juce::ValueTree();
        }
        void stageLayerPresetTree(int slot, juce::ValueTree tree, bool) override { layerSwapper.useSlot(slot, std::move(tree)); }
        void loadLayerPreset(int, const juce::File&) override {}

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
        void startPlaying() { layerSwapper.flagBoundaries(true, 0.0, 0.1); }
        void landLaunch()
        {
            layerSwapper.flagBoundaries(true, 3.95, 0.2);
            layerSwapper.commit();
            getLiveState().reconcile();
        }
        void stageNonClip(int slot, juce::ValueTree tree) { stageLayerPreset(slot, std::move(tree), false, false); }

        juce::String getLayerName(int) const override { return {}; }
        int getLayerColourIndex(int i) const override { return layers[(size_t) i] ? layers[(size_t) i]->colourIndex : 0; }
        juce::File   getLayerPresetDir()       const override { return {}; }
        juce::String getLayerPresetExtension() const override { return ".muTest"; }
        juce::String getFullPresetExtension()  const override { return ".muTest"; }
        void applyFullMidiPreset(const juce::File&) override {}
        const juce::String getName() const override          { return "autosave-test"; }
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

    using T = mu_core::ControlActionType;

    mu_core::ControlAction act(T type, int layer = 0, int index = -1)
    {
        mu_core::ControlAction a;
        a.type = type; a.layer = layer; a.index = index;
        return a;
    }

    // Layer 0 with clip 2 stored at 0.2 (not lit) and clip 1 stored at 0.9 and lit; the live layer is 0.9.
    void twoClips(AutoSaveProcessor& p)
    {
        p.setParam("v0_a", 0.2f);
        p.storeClip(0, 2, "two");
        p.setParam("v0_a", 0.9f);
        p.perform(act(T::StoreClip, 0, 1));
    }
}

struct ClipAutoSaveTests : juce::UnitTest
{
    ClipAutoSaveTests() : juce::UnitTest("ProcessorBase clip auto-save", "mu-core") {}

    // An edit becomes the candidate on one step and is written on the next, and only to the lit clip.
    void editThenSettle()
    {
        beginTest("an edit is written to the lit clip once the layer has held still for a step, and to nothing else");
        AutoSaveProcessor p;
        twoClips(p);
        const auto otherBefore = p.layers[0]->clips->get(2);
        for (int i = 0; i < 4; ++i)
            expect(! p.stepClipAutoSave(), "an unchanged layer is never written");

        p.setParam("v0_a", 0.5f);
        const auto stored = p.layers[0]->clips->get(1);
        expect(! p.stepClipAutoSave(), "the first step only sees it");
        expect(p.layers[0]->clips->get(1).isEquivalentTo(stored), "not stored yet");
        expect(p.stepClipAutoSave(), "the second step finds it still");
        expect(p.layers[0]->clips->get(1).isEquivalentTo(p.captureLayerPresetTree(0)));
        expect(! p.layers[0]->clips->get(1).isEquivalentTo(stored));
        expect(p.layers[0]->clips->get(2).isEquivalentTo(otherBefore), "the other clip is untouched");
        for (int i = 0; i < 4; ++i)
            expect(! p.stepClipAutoSave(), "and it is not rewritten");
    }

    void dragInFlightIsNotStored()
    {
        beginTest("a layer still moving between steps is not stored until it holds still");
        AutoSaveProcessor p;
        twoClips(p);
        const auto stored = p.layers[0]->clips->get(1);
        p.setParam("v0_a", 0.5f);
        expect(! p.stepClipAutoSave());
        p.setParam("v0_a", 0.6f);
        expect(! p.stepClipAutoSave(), "a different capture only replaces the candidate");
        expect(p.layers[0]->clips->get(1).isEquivalentTo(stored));
        expect(p.stepClipAutoSave());
        expectWithinAbsoluteError(p.param("v0_a"), 0.6f, 0.01f);
        p.perform(act(T::LaunchClip, 0, 2));
        p.perform(act(T::LaunchClip, 0, 1));
        expectWithinAbsoluteError(p.param("v0_a"), 0.6f, 0.01f);
    }

    void switchAwayAndBack()
    {
        beginTest("switching away and back restores the edit, and a launch does not rewrite the clip it loads");
        AutoSaveProcessor p;
        twoClips(p);
        p.setParam("v0_a", 0.5f);
        p.stepClipAutoSave();
        p.stepClipAutoSave();

        const auto one = p.layers[0]->clips->get(1);
        const auto two = p.layers[0]->clips->get(2);
        expect(p.perform(act(T::LaunchClip, 0, 2)));
        expectWithinAbsoluteError(p.param("v0_a"), 0.2f, 0.01f);
        for (int i = 0; i < 4; ++i)
            expect(! p.stepClipAutoSave(), "the migrated launch is adopted, not written");
        expect(p.layers[0]->clips->get(2).isEquivalentTo(two));
        expect(p.layers[0]->clips->get(1).isEquivalentTo(one));

        expect(p.perform(act(T::LaunchClip, 0, 1)));
        expectWithinAbsoluteError(p.param("v0_a"), 0.5f, 0.01f);
        for (int i = 0; i < 4; ++i)
            expect(! p.stepClipAutoSave());
        expect(p.layers[0]->clips->get(1).isEquivalentTo(one));
    }

    void editDuringQueuedLaunch()
    {
        beginTest("an edit made while a launch is queued lands on the outgoing clip; the commit adopts the new one");
        AutoSaveProcessor p;
        twoClips(p);
        const auto two = p.layers[0]->clips->get(2);
        p.startPlaying();
        expect(p.perform(act(T::LaunchClip, 0, 2)));
        expect(p.hasPendingSwap(0));
        p.setParam("v0_a", 0.7f);
        p.stepClipAutoSave();
        expect(p.stepClipAutoSave(), "the live layer is still the lit clip");
        expect(p.layers[0]->clips->get(1).isEquivalentTo(p.captureLayerPresetTree(0)));
        expect(p.layers[0]->clips->get(2).isEquivalentTo(two));

        p.landLaunch();
        expectWithinAbsoluteError(p.param("v0_a"), 0.2f, 0.01f);
        for (int i = 0; i < 3; ++i)
            expect(! p.stepClipAutoSave(), "nothing is written for the landing");
        expect(p.layers[0]->clips->get(2).isEquivalentTo(two));
        expect(! p.layers[0]->clips->get(1).isEquivalentTo(two));

        p.setParam("v0_a", 0.4f);
        p.stepClipAutoSave();
        expect(p.stepClipAutoSave(), "after the commit edits go to the new clip");
        expect(p.layers[0]->clips->get(2).isEquivalentTo(p.captureLayerPresetTree(0)));
    }

    void nothingLitWritesNothing()
    {
        beginTest("with no lit clip nothing is written, and a layer-preset load detaches the lit clip");
        {
            AutoSaveProcessor p;
            p.storeClip(0, 1, "kept");   // stored but not lit
            const auto stored = p.layers[0]->clips->get(1);
            p.setParam("v0_a", 0.8f);
            for (int i = 0; i < 4; ++i)
                expect(! p.stepClipAutoSave());
            expect(p.layers[0]->clips->get(1).isEquivalentTo(stored));
        }
        {
            AutoSaveProcessor p;
            twoClips(p);
            const auto stored = p.layers[0]->clips->get(1);
            p.setParam("v0_a", 0.3f);
            p.stageNonClip(0, p.captureLayerPresetTree(0));   // a layer preset loaded from the dropdown
            expectEquals(p.getLiveState().getPlayingClip(0), -1, "the playing marker clears");
            p.setParam("v0_a", 0.6f);
            for (int i = 0; i < 4; ++i)
                expect(! p.stepClipAutoSave());
            expect(p.layers[0]->clips->get(1).isEquivalentTo(stored), "the bank is untouched");
        }
    }

    void settingAndOfflineRender()
    {
        beginTest("the setting off, and an offline render, write nothing");
        AutoSaveProcessor p;
        expect(p.isClipAutoSaveOn(), "on by default");
        twoClips(p);
        const auto stored = p.layers[0]->clips->get(1);

        p.setClipAutoSave(false);
        p.setParam("v0_a", 0.5f);
        for (int i = 0; i < 4; ++i)
            expect(! p.stepClipAutoSave());
        p.flushClipAutoSave();
        expect(p.layers[0]->clips->get(1).isEquivalentTo(stored), "setting off: neither the poll nor a flush writes");

        p.setClipAutoSave(true);
        p.setNonRealtime(true);
        for (int i = 0; i < 4; ++i)
            expect(! p.stepClipAutoSave());
        p.flushClipAutoSave();
        expect(p.layers[0]->clips->get(1).isEquivalentTo(stored), "offline render writes nothing");

        p.setNonRealtime(false);
        p.stepClipAutoSave();   // adopts the edited layer as the new baseline: a lapse never writes what was missed
        expect(! p.stepClipAutoSave());
        expect(p.layers[0]->clips->get(1).isEquivalentTo(stored));
    }

    void performanceMode()
    {
        beginTest("Performance mode: flushed on entry, nothing written while on, re-adopted on leaving");
        const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("mu_autosave_" + juce::String(juce::Time::currentTimeMillis()));
        dir.createDirectory();
        juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Standalone);
        AutoSaveProcessor p;
        juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Undefined);
        mu_pp::PerformanceFile list;
        list.setItem(0, p.getProductStateId(), dir.getChildFile("p.preset"), dir, "P");
        p.loadPerformance(list, dir.getChildFile("live.muPerformance"));

        twoClips(p);
        p.setParam("v0_a", 0.5f);   // an edit the poll has not stored yet
        expect(p.perform(act(T::PerformanceMode, 0, 1)), "mode on");
        expect(p.layers[0]->clips->get(1).isEquivalentTo(p.captureLayerPresetTree(0)), "flushed on entry");

        const auto stored = p.layers[0]->clips->get(1);
        p.setParam("v0_a", 0.1f);
        for (int i = 0; i < 4; ++i)
            expect(! p.stepClipAutoSave(), "off while locked");
        expect(p.layers[0]->clips->get(1).isEquivalentTo(stored));

        expect(p.perform(act(T::PerformanceMode, 0, 0)), "mode off");
        for (int i = 0; i < 3; ++i)
            expect(! p.stepClipAutoSave(), "the layer is adopted as it is, nothing is written");
        expect(p.layers[0]->clips->get(1).isEquivalentTo(stored));
        dir.deleteRecursively();
    }

    void flushes()
    {
        beginTest("a flush puts a late edit in the bank before a launch, a copy and a state capture");
        {
            AutoSaveProcessor p;
            twoClips(p);
            p.setParam("v0_a", 0.4f);   // never settled
            expect(p.perform(act(T::LaunchClip, 0, 2)));
            expectWithinAbsoluteError(p.param("v0_a"), 0.2f, 0.01f);
            p.perform(act(T::LaunchClip, 0, 1));
            expectWithinAbsoluteError(p.param("v0_a"), 0.4f, 0.01f);
        }
        {
            AutoSaveProcessor p;
            twoClips(p);
            mu_core::clipClipboard().clear();
            p.setParam("v0_a", 0.4f);
            expect(p.copyClip(0, 1));
            AutoSaveProcessor q;
            q.setParam("v0_a", 0.0f);
            expect(q.pasteClip(0, 3));
            expect(q.launchClip(0, 3));
            expectWithinAbsoluteError(q.param("v0_a"), 0.4f, 0.01f);
            mu_core::clipClipboard().clear();
        }
        {
            AutoSaveProcessor p;
            twoClips(p);
            p.setParam("v0_a", 0.4f);
            p.captureComposedState();
            expect(p.layers[0]->clips->get(1).isEquivalentTo(p.captureLayerPresetTree(0)), "the saved bank is current");
        }
    }

    void storeWhileStaged()
    {
        beginTest("an explicit Store that took a staged tree is not overwritten by the old live state");
        AutoSaveProcessor src;
        src.setParam("v0_a", 0.8f);
        const auto presetA = src.captureLayerPresetTree(0);

        AutoSaveProcessor p;
        twoClips(p);
        p.startPlaying();
        p.stageNonClip(0, presetA);   // a preset waiting for its boundary
        expect(p.perform(act(T::StoreClip, 0, 3)));
        const auto stored = p.layers[0]->clips->get(3);
        expectEquals(p.getLiveState().getPlayingClip(0), 3);
        for (int i = 0; i < 4; ++i)
            expect(! p.stepClipAutoSave(), "the live layer is not the staged preset yet");
        expect(p.layers[0]->clips->get(3).isEquivalentTo(stored));

        p.landLaunch();
        for (int i = 0; i < 3; ++i)
            expect(! p.stepClipAutoSave(), "the commit is adopted");
        expect(p.layers[0]->clips->get(3).isEquivalentTo(stored));
        p.setParam("v0_a", 0.3f);
        p.stepClipAutoSave();
        expect(p.stepClipAutoSave(), "and edits after it are saved");
    }

    void structuralEdits()
    {
        beginTest("a structural edit flushes the lit layers first and drops the trackers");
        AutoSaveProcessor p;
        p.setParam("v1_a", 0.6f);
        p.perform(act(T::StoreClip, 1, 4));   // layer 1, clip 4 lit
        p.setParam("v1_a", 0.7f);             // unsettled edit
        expect(p.swapLayers(0, 1));
        expectEquals(p.getLiveState().getPlayingClip(0), 4, "the lit clip follows its layer");
        expect(p.layers[0]->clips->get(4).isEquivalentTo(p.captureLayerPresetTree(0)), "the edit was flushed before the swap");
        for (int i = 0; i < 3; ++i)
            expect(! p.stepClipAutoSave());
        p.setParam("v0_a", 0.1f);
        p.stepClipAutoSave();
        expect(p.stepClipAutoSave(), "the tracker came back on the new slot");
        expect(p.layers[0]->clips->get(4).isEquivalentTo(p.captureLayerPresetTree(0)));

        const auto before = p.layers[0]->clips->get(4);
        expect(p.addLayer() >= 0);
        for (int i = 0; i < 3; ++i)
            expect(! p.stepClipAutoSave());
        expect(p.layers[0]->clips->get(4).isEquivalentTo(before));

        p.resetLayer(0);
        expectEquals(p.getLiveState().getPlayingClip(0), -1);
        for (int i = 0; i < 3; ++i)
            expect(! p.stepClipAutoSave());
        expectEquals((int) p.clipMask(0), 0, "a reset empties the bank and nothing refills it");
    }

    void pasteOntoLit()
    {
        beginTest("a paste onto the lit clip is not overwritten by the old live layer before the relaunch lands");
        mu_core::clipClipboard().clear();
        {
            AutoSaveProcessor p;
            p.setParam("v1_a", 0.9f);
            p.storeClip(1, 0, "src");
            twoClips(p);
            expect(p.copyClip(1, 0));
            p.setParam("v0_a", 0.5f);   // an unsettled edit of the lit clip about to be pasted over
            p.startPlaying();
            expect(p.pasteClip(0, 1));
            const auto pasted = p.layers[0]->clips->get(1);
            expectEquals(p.layers[0]->clips->getName(1), juce::String("src"));
            for (int i = 0; i < 4; ++i)
                expect(! p.stepClipAutoSave(), "suspended until the relaunch lands");
            expect(p.layers[0]->clips->get(1).isEquivalentTo(pasted));

            p.landLaunch();
            expectWithinAbsoluteError(p.param("v0_a"), 0.9f, 0.01f);
            for (int i = 0; i < 3; ++i)
                expect(! p.stepClipAutoSave());
            expect(p.layers[0]->clips->get(1).isEquivalentTo(pasted));
            p.setParam("v0_a", 0.3f);
            p.stepClipAutoSave();
            expect(p.stepClipAutoSave(), "auto-save resumes once it landed");
        }
        mu_core::clipClipboard().clear();
    }

    void runTest() override
    {
        editThenSettle();
        dragInFlightIsNotStored();
        switchAwayAndBack();
        editDuringQueuedLaunch();
        nothingLitWritesNothing();
        settingAndOfflineRender();
        performanceMode();
        flushes();
        storeWhileStaged();
        structuralEdits();
        pasteOntoLit();
    }
};

static ClipAutoSaveTests clipAutoSaveTests;
