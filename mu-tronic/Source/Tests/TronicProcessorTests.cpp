// Tests on the REAL mu-Tronic PluginProcessor, built headless (no editor, host or audio device): sixteen Empty
// slots after construction, the layer type registry, names, a type change through a registered test type, the
// published view, and the saved state round trip.

#include "Plugin/PluginProcessor.h"
#include "Tests/ProcessorSeamHelpers.h"

#include <cmath>
#include <memory>
#include <set>

// The product class names its editor in a virtual, defined in PluginEditor.cpp, which this headless target does
// not link (see "Headless product processors" in design-plugin-family.md).
juce::AudioProcessorEditor* mu_tronic::PluginProcessor::createEditor() { return nullptr; }

namespace
{
    static_assert(mu_limits::kMaxLayers == 16, "mu-tronic compiles with MU_MAX_LAYERS=16");
    static_assert(mu_limits::kMaxLayers % mu_limits::kLayersPerPage == 0, "the pages are whole");

    // A layer type the build does not have: a constant tone, so a type change is heard in the output.
    struct ToneLayer : Layer
    {
        int typeId() const override { return (int) mu_core::LayerTypeId::Arp; }
        void prepare(double, int blockSize) override { preparedBlock = blockSize; ++prepares; }
        void render(const mu_core::RenderContext&, juce::AudioBuffer<float>& out, int n) override
        {
            for (int c = 0; c < 2; ++c)
                juce::FloatVectorOperations::fill(out.getWritePointer(c), 0.25f, n);
        }
        int preparedBlock = 0, prepares = 0;
    };

    // The real processor with the layer storage and the registry exposed, and a test type registered.
    struct ExposedProcessor : mu_tronic::PluginProcessor
    {
        ExposedProcessor(bool registerTone = true)
        {
            if (registerTone)
            {
                mu_core::LayerTypeInfo info;
                info.id = mu_core::LayerTypeId::Arp;
                info.name = "Arp";
                info.group = "Test";
                info.create = [](int) -> std::unique_ptr<Layer> { return std::make_unique<ToneLayer>(); };
                layerTypes.add(info);
            }
        }
        using mu_tronic::PluginProcessor::layers;
        using mu_tronic::PluginProcessor::layerAccess;
        using ProcessorCore::getLayerAccess;
        using mu_tronic::PluginProcessor::getNumActiveLayers;
        bool isStandaloneApp() const noexcept override { return true; }
        ToneLayer& tone(int i) { return static_cast<ToneLayer&>(*layers[(size_t) i]); }
    };

    float peakOf(const juce::AudioBuffer<float>& b)
    {
        float p = 0.0f;
        for (int c = 0; c < b.getNumChannels(); ++c) p = juce::jmax(p, b.getMagnitude(c, 0, b.getNumSamples()));
        return p;
    }

    // Renders `blocks` blocks of 256 frames and returns the peak of the last.
    float renderPeak(ExposedProcessor& p, int blocks = 3)
    {
        juce::AudioBuffer<float> buf(2, 256);
        juce::MidiBuffer midi;
        float last = 0.0f;
        for (int b = 0; b < blocks; ++b)
        {
            buf.clear();
            midi.clear();
            p.processBlock(buf, midi);
            last = peakOf(buf);
        }
        return last;
    }
}

struct MuTronicProcessorTests : juce::UnitTest
{
    MuTronicProcessorTests() : juce::UnitTest("mu-Tronic real PluginProcessor", "mu-tronic-processor") {}

    void runTest() override
    {
        constructedEmpty();
        namesAndColours();
        registryAndTypeChange();
        deleteEmptiesSlot();
        stateRoundTrip();
        unknownTypeLoadsEmpty();
    }

    // Sixteen slots, each an Empty layer, in Published mode; the driver sees all sixteen and the output is silent.
    void constructedEmpty()
    {
        beginTest("a fresh instance holds 16 Empty layers in Published mode and renders silence");
        mu_seam_test::TempStorage storage;
        ExposedProcessor p;
        expectEquals(p.getNumLayers(), 16);
        expectEquals(p.layoutWordCountForTest(), 16);
        expect(p.getLayerAccess() == mu_core::LayerAccess::Published);
        expect(p.layersAreSlots());
        expect(! p.canReorderLayers());
        expect(p.getLayerTypes().size() == 1, "the test type is the only one offered");
        for (int i = 0; i < 16; ++i)
        {
            expect(p.getLayer(i) != nullptr);
            expect(p.getLayerTypeId(i) == mu_core::LayerTypeId::Empty);
        }
        expectEquals(p.getNumActiveLayers(), 0);

        p.prepareToPlay(48000.0, 256);
        p.toggleInternalPlay();
        expectEquals(renderPeak(p), 0.0f);
        const auto probe = p.getLayerRenderProbe();
        expectEquals(probe.mixCount, 16, "every slot is handed to the mixer");
        expect(probe.blocksRendered >= 3);
        p.releaseResources();

        mu_seam_test::TempStorage storage2;
        mu_tronic::PluginProcessor bare;   // the product as built: nothing registered, so only Empty exists
        expect(bare.getLayerTypes().isEmpty());
    }

    void namesAndColours()
    {
        beginTest("slot names default to Slot N, a typed name stays, the default can be restored");
        mu_seam_test::TempStorage storage;
        ExposedProcessor p;
        expectEquals(p.getLayerName(0), juce::String("Slot 1"));
        expectEquals(p.getLayerName(15), juce::String("Slot 16"));
        p.setLayerName(4, "  Lead  ");
        expectEquals(p.getLayerName(4), juce::String("Lead"));
        p.setLayerName(4, "Slot 5");   // typing the default back derives again
        expect(p.getLayer(4)->name.empty());
        p.setLayerName(4, "Pad");
        p.setLayerName(4, "");
        expectEquals(p.getLayerName(4), juce::String("Slot 5"));
        expectEquals(p.getLayerName(99), juce::String());

        beginTest("a page of eight slots has eight distinct colours");
        for (int page = 0; page < 2; ++page)
        {
            std::set<int> seen;
            for (int i = 0; i < 8; ++i) seen.insert(p.getLayerColourIndex(page * 8 + i));
            expectEquals((int) seen.size(), 8);
        }
    }

    // A type the build offers goes in without muting; one it does not offer is refused; Empty empties.
    void registryAndTypeChange()
    {
        beginTest("setLayerType refuses an unoffered type and a slot out of range, and treats the same type as a no-op");
        {
            mu_seam_test::TempStorage storage;
            mu_tronic::PluginProcessor bare;
            expect(! bare.setLayerType(0, mu_core::LayerTypeId::Arp), "nothing is registered");
            expect(bare.setLayerType(0, mu_core::LayerTypeId::Empty), "Empty to Empty is a no-op that succeeds");
            expect(! bare.setLayerType(16, mu_core::LayerTypeId::Empty));
            expect(! bare.setLayerType(-1, mu_core::LayerTypeId::Empty));
        }

        beginTest("a layer of an offered type sounds at once, keeps the strip, and Empty silences it");
        mu_seam_test::TempStorage storage;
        ExposedProcessor p;
        p.prepareToPlay(48000.0, 256);
        p.toggleInternalPlay();
        mu_seam_test::setParam(p.apvts, "ch3_lvl", 0.5f);
        p.setLayerName(3, "Lead");
        expectEquals(renderPeak(p), 0.0f);

        expect(p.setLayerType(3, mu_core::LayerTypeId::Arp));
        expect(p.getLayerTypeId(3) == mu_core::LayerTypeId::Arp);
        expectEquals(p.getNumActiveLayers(), 1);
        expectEquals(p.getLayerName(3), juce::String("Lead"), "a typed name stays with the slot");
        expectEquals(p.tone(3).preparedBlock, 256, "the new layer is prepared for the running block size");
        expect(renderPeak(p) > 0.01f, "the new layer is in the output");
        expectWithinAbsoluteError(mu_seam_test::getParam(p.apvts, "ch3_lvl"), 0.5f, 0.001f);   // the strip is kept

        // The default name follows the type.
        p.setLayerName(3, "");
        expectEquals(p.getLayerName(3), juce::String("Arp 4"));

        expect(p.deleteLayer(3));
        expect(p.getLayerTypeId(3) == mu_core::LayerTypeId::Empty);
        expectEquals(p.getNumLayers(), 16, "nothing shifts");
        expectEquals(renderPeak(p), 0.0f, "an Empty slot renders silence (the Arp layer has no tail)");
        p.flushPendingAsyncUpdates();
        p.releaseResources();
    }

    void deleteEmptiesSlot()
    {
        beginTest("deleting a layer leaves the layers around it where they were");
        mu_seam_test::TempStorage storage;
        ExposedProcessor p;
        for (int s : { 1, 2, 3 }) expect(p.setLayerType(s, mu_core::LayerTypeId::Arp));
        p.setLayerName(1, "One");
        p.setLayerName(3, "Three");
        const auto* before1 = p.getLayer(1);
        const auto* before3 = p.getLayer(3);
        expect(p.deleteLayer(2));
        expect(p.getLayer(1) == before1 && p.getLayer(3) == before3, "the neighbours are the same objects");
        expectEquals(p.getLayerName(1), juce::String("One"));
        expectEquals(p.getLayerName(3), juce::String("Three"));
        expect(p.getLayerTypeId(2) == mu_core::LayerTypeId::Empty);
        expectEquals(p.getNumActiveLayers(), 2);
    }

    // The saved state carries each slot's type and typed name; a second instance restores them.
    void stateRoundTrip()
    {
        beginTest("a state round trip keeps types, typed names, colours and strip values");
        mu_seam_test::TempStorage storage;
        ExposedProcessor a;
        expect(a.setLayerType(2, mu_core::LayerTypeId::Arp));
        expect(a.setLayerType(9, mu_core::LayerTypeId::Arp));
        a.setLayerName(2, "Bass line");
        a.getLayer(9)->colourIndex = 5;
        mu_seam_test::setParam(a.apvts, "ch9_lvl", 0.25f);
        mu_seam_test::setParam(a.apvts, "mstr_lvl", 0.6f);

        const auto state = a.captureState();
        expectEquals(state.getType().toString(), juce::String("MuTronicState"));
        expectEquals((int) state.getProperty("numVoices"), 16);
        expectEquals((int) state.getProperty("format"), 2);
        const auto slots = state.getChildWithName(mu_pp::kLayersTag);
        expectEquals(slots.getNumChildren(), 16);
        expectEquals(mu_pp::findLayerNode(state, 2).getProperty("type").toString(), juce::String("Arp"));
        expectEquals(mu_pp::findLayerNode(state, 2).getProperty("name").toString(), juce::String("Bass line"));
        expectEquals(mu_pp::findLayerNode(state, 3).getProperty("type").toString(), juce::String("Empty"));
        expect(! mu_pp::findLayerNode(state, 3).hasProperty("name"), "a default name is not written");

        // Through the binary session form, as the standalone saves it.
        juce::MemoryBlock block;
        a.getStateInformation(block);

        mu_seam_test::TempStorage storage2;
        ExposedProcessor b;
        b.setStateInformation(block.getData(), (int) block.getSize());
        expect(b.getLayerTypeId(2) == mu_core::LayerTypeId::Arp);
        expect(b.getLayerTypeId(9) == mu_core::LayerTypeId::Arp);
        expect(b.getLayerTypeId(3) == mu_core::LayerTypeId::Empty);
        expectEquals(b.getNumActiveLayers(), 2);
        expectEquals(b.getLayerName(2), juce::String("Bass line"));
        expectEquals(b.getLayerName(9), juce::String("Arp 10"));
        expectEquals(b.getLayerColourIndex(9), 5);
        expectWithinAbsoluteError(mu_seam_test::getParam(b.apvts, "ch9_lvl"), 0.25f, 0.001f);
        expectWithinAbsoluteError(mu_seam_test::getParam(b.apvts, "mstr_lvl"), 0.6f, 0.001f);
        expectEquals(b.getNumLayers(), 16);

        beginTest("loading a state into a processor that already holds other layers replaces them");
        expect(b.setLayerType(12, mu_core::LayerTypeId::Arp));
        b.setLayerName(12, "Stray");
        b.setStateInformation(block.getData(), (int) block.getSize());
        expect(b.getLayerTypeId(12) == mu_core::LayerTypeId::Empty, "the stray layer is gone");
        expectEquals(b.getLayerName(12), juce::String("Slot 13"));
        b.flushPendingAsyncUpdates();
    }

    void unknownTypeLoadsEmpty()
    {
        beginTest("a state naming a type the build does not offer, or none, loads that slot Empty");
        mu_seam_test::TempStorage storage;
        ExposedProcessor withTone;
        expect(withTone.setLayerType(1, mu_core::LayerTypeId::Arp));
        auto state = withTone.captureState();
        mu_pp::findLayerNode(state, 1).setProperty("type", "Hat", nullptr);       // a name the build has no type for
        mu_pp::findLayerNode(state, 5).removeProperty("type", nullptr);            // no attribute at all

        mu_seam_test::TempStorage storage2;
        ExposedProcessor other;
        other.applyStateTree(state);
        for (int i = 0; i < 16; ++i)
            expect(other.getLayerTypeId(i) == mu_core::LayerTypeId::Empty);

        // The product as built offers nothing: even a state that names a type it knows loads Empty.
        mu_seam_test::TempStorage storage3;
        mu_tronic::PluginProcessor bare;
        auto known = withTone.captureState();
        bare.applyStateTree(known);
        expect(bare.getLayerTypeId(1) == mu_core::LayerTypeId::Empty);
    }
};

static MuTronicProcessorTests muTronicProcessorTests;
