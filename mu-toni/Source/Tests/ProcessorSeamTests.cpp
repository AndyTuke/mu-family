// Tests on the REAL mu-Toni PluginProcessor, built headless (no editor, host or audio device):
// construction under a temp storage root and layer ownership after construction.

#include "Plugin/PluginProcessor.h"
#include "Tests/ProcessorSeamHelpers.h"

#include <set>

// The product class names its editor in a virtual, defined in PluginEditor.cpp, which this headless
// target does not link (see "Headless product processors" in design-plugin-family.md).
juce::AudioProcessorEditor* mu_toni::PluginProcessor::createEditor() { return nullptr; }

namespace
{
    // The real processor with the layer storage exposed to the tests.
    struct ExposedPluginProcessor : mu_toni::PluginProcessor
    {
        using mu_toni::PluginProcessor::layers;
        using mu_toni::PluginProcessor::numLayers;
        // Performance mode is standalone-only; the test stands in for the standalone wrapper.
        bool isStandaloneApp() const noexcept override { return true; }
    };
}

struct MuToniProcessorSeamTests : juce::UnitTest
{
    MuToniProcessorSeamTests() : juce::UnitTest("mu-Toni real PluginProcessor", "mu-toni-processor") {}

    // The shared swap-timing parameter exists in every product, with that product's default.
    void swapTimingDefault()
    {
        beginTest("mstr_swap exists and defaults to OnBar in mu-Toni");
        mu_seam_test::TempStorage storage;
        ExposedPluginProcessor p;
        auto* swap = p.apvts.getParameter("mstr_swap");
        expect(swap != nullptr, "the swap timing parameter is declared");
        if (swap != nullptr)
            expectEquals((int) std::lround(swap->convertFrom0to1(swap->getDefaultValue())), 2);
    }

    // Performance mode through the real wiring: a PerformanceFile of two saved presets, the mode entered and
    // PerformanceItem performed against the real processor, with the transport stopped (applies at once).
    void performanceItems()
    {
        beginTest("Performance mode on the real processor: items load their presets, a missing file is refused, a plugin refuses the mode");
        mu_seam_test::TempStorage storage;
        ExposedPluginProcessor p;
        const auto dir = p.getFullPresetDir();
        dir.createDirectory();

        // Two presets that differ in one parameter.
        const auto fileA = dir.getChildFile("perfA.muToni"), fileB = dir.getChildFile("perfB.muToni");
        mu_seam_test::setParam(p.apvts, "mstr_swap", 0.0f);
        expect(p.saveFullPresetTo(fileA));
        mu_seam_test::setParam(p.apvts, "mstr_swap", 1.0f);
        expect(p.saveFullPresetTo(fileB));
        const auto product = p.apvts.state.getType().toString();

        mu_pp::PerformanceFile list;
        list.setItem(0, product, fileA, dir, "A");
        list.setItem(1, product, fileB, dir, "B");
        list.setItem(2, product, dir.getChildFile("gone.muToni"), dir, "Gone");
        const auto gig = dir.getChildFile("gig.muPerformance");
        expect(list.save(gig), "the performance file is written (a reload reads it back from disk)");
        expect(p.getLiveState().loadPerformance(list, gig));

        auto action = [](mu_core::ControlActionType t, int index)
        {
            mu_core::ControlAction a; a.type = t; a.index = index; return a;
        };
        using T = mu_core::ControlActionType;

        mu_seam_test::setParam(p.apvts, "mstr_swap", 2.0f);
        expect(p.perform(action(T::PerformanceMode, 1)), "standalone: the mode enters");
        expect(p.perform(action(T::PerformanceItem, 0)), "item 0 loads");
        expectEquals(p.getLiveState().getCurrentItem(), 0, "stopped transport: applied at once");
        expectEquals(p.getLiveState().getQueuedItem(), -1);
        expectEquals((int) mu_seam_test::getParam(p.apvts, "mstr_swap"), 0, "the preset's value is live");
        expect(p.perform(action(T::PerformanceNext, -1)), "Next steps to item 1");
        expectEquals(p.getLiveState().getCurrentItem(), 1);
        expectEquals((int) mu_seam_test::getParam(p.apvts, "mstr_swap"), 1);
        expect(! p.perform(action(T::PerformanceItem, 2)), "an item whose file is gone is refused");
        expect(! p.perform(action(T::PerformanceItem, 5)), "an empty item is refused");
        expectEquals(p.getLiveState().getCurrentItem(), 1, "and nothing changed");
        expect(p.perform(action(T::PerformanceNext, -1)), "Next wraps past the missing and empty items to 0");
        expectEquals(p.getLiveState().getCurrentItem(), 0);

        // The session keeps the gig across a restart.
        juce::ValueTree root("MuToniState");
        p.saveLiveState(root);
        ExposedPluginProcessor q;
        q.restoreLiveState(root);
        expect(q.getLiveState().isPerformanceMode(), "the mode returns on a reload");
        expectEquals(q.getLiveState().getCurrentItem(), 0);
        expectEquals(q.getLiveState().getPerformance().count(), 3);

        // A plugin (not the standalone) never enters the mode.
        struct PluginInstance : mu_toni::PluginProcessor
        {
            bool isStandaloneApp() const noexcept override { return false; }
        } plugin;
        expect(plugin.getLiveState().loadPerformance(list, dir.getChildFile("gig.muPerformance")));
        expect(! plugin.perform(action(T::PerformanceMode, 1)), "a plugin refuses the mode");
    }

    void runTest() override
    {
        swapTimingDefault();
        performanceItems();
        using namespace mu_seam_test;
        beginTest("A fresh instance has one layer; every slot's Arp exists and getLayer(i) is getArp(i)");
        TempStorage storage;
        ExposedPluginProcessor p;
        expectEquals(p.numLayers.load(), 1, "a fresh instance starts with one layer");
        expectEquals(p.getNumLayers(), 1);
        expect(p.getLayer(1) == nullptr && p.getLayer(-1) == nullptr, "no layer above the live count or below zero");
        std::set<const void*> seen;
        for (int i = 0; i < mu_limits::kMaxLayers; ++i)
        {
            expect(p.layers[(size_t) i] != nullptr, "slot " + juce::String(i) + " is allocated");
            expect(&p.getArp(i) == p.layers[(size_t) i].get(), "getArp(" + juce::String(i) + ") is layer " + juce::String(i));
            seen.insert(&p.getArp(i));
        }
        expect(p.getLayer(0) == &p.getArp(0), "getLayer(0) is getArp(0)");
        expectEquals((int) seen.size(), mu_limits::kMaxLayers, "distinct arps");

        beginTest("prepareToPlay and a processBlock run under the temp root");
        p.prepareToPlay(48000.0, 256);
        juce::AudioBuffer<float> buf(2, 256);
        juce::MidiBuffer midi;
        p.processBlock(buf, midi);
        expect(bufferIsSane(buf), "output is finite");
        expectEquals(p.getContentDir().getFullPathName(),
                     storage.root.getChildFile("content").getChildFile("muToni").getFullPathName());

        beginTest("a slot that goes live starts from silence, not from the last tenant's voice");
        {
            ExposedPluginProcessor q;
            q.prepareToPlay(48000.0, 256);
            mu_toni::ArpContext ctx; ctx.playing = true; ctx.sampleRate = 48000.0; ctx.beatsPerSample = 120.0 / 60.0 / 48000.0;
            juce::AudioBuffer<float> blk(2, 256);
            auto& spare = q.getArp(1).runner;                 // a spare slot above the live count
            for (int b = 0; b < 40; ++b) { ctx.startBeat = b * 256 * ctx.beatsPerSample; spare.render(blk, 256, ctx); }
            expect(spare.isActive(), "the spare slot's voice is sounding");
            q.numLayers.store(2);                              // the slot goes live
            juce::MidiBuffer noMidi;
            q.processBlock(blk, noMidi);
            expect(! q.getArp(1).runner.isActive(), "its voice was reset when it went live");
        }

        // Add / remove / swap on this thread while a worker renders, at every block size.
        // A running host transport makes the arp sound (it is silent while stopped).
        RunningPlayHead playHead(48000.0, 512);
        ExposedPluginProcessor stress;
        stress.setPlayHead(&playHead);
        runStructuralEditStress(*this, stress,
            [&](int layer) { juce::ignoreUnused(layer); },   // every layer sounds at its defaults once the transport runs
            [&](const juce::String& where)
            {
                const int n = stress.numLayers.load();
                expect(n >= 1 && n <= mu_limits::kMaxLayers, where + ": count in range");
                std::set<const void*> distinct;
                for (int i = 0; i < mu_limits::kMaxLayers; ++i)
                {
                    expect(stress.layers[(size_t) i] != nullptr, where + ": slot " + juce::String(i) + " is filled");
                    distinct.insert(stress.layers[(size_t) i].get());
                }
                expectEquals((int) distinct.size(), mu_limits::kMaxLayers, where + ": no layer appears twice");
            });
    }
};

static MuToniProcessorSeamTests muToniProcessorSeamTests;
