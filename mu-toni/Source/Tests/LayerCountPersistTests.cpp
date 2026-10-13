// Layer count and structural edits on the REAL mu-Toni PluginProcessor, built headless: a fresh
// instance starts with one layer, states saved when the count was fixed at four still load as four,
// the count and palette colours round-trip, and add / remove / swap carry a layer's parameters,
// colour and mixer strip with it. (createEditor is defined in ProcessorSeamTests.cpp.)

#include "Plugin/PluginProcessor.h"
#include "Tests/ProcessorSeamHelpers.h"

namespace
{
    // The real processor with the layer storage exposed to the tests.
    struct ExposedPluginProcessor : mu_toni::PluginProcessor
    {
        using mu_toni::PluginProcessor::layers;
        using mu_toni::PluginProcessor::numLayers;
    };

    // A format-2 state as saved while the count was fixed at four: no numVoices, no voiceColours.
    const char* const kOldComposedState = R"(<MuToniState format="2">
  <Globals>
    <p id="ch0_lvl" x="0.5"/>
  </Globals>
  <Slots>
    <Slot idx="0"><p id="cut" x="777.0"/></Slot>
    <Slot idx="1"><p id="aeA" x="0.5"/></Slot>
    <Slot idx="2"><p id="scale" x="5.0"/></Slot>
    <Slot idx="3"><p id="leg" x="1.0"/></Slot>
  </Slots>
</MuToniState>)";

    // An older APVTS dump (ids still "v{N}_"), also from before the count was variable.
    const char* const kOldApvtsDump = R"(<MuToniState>
  <PARAM id="ch0_lvl" value="0.5"/>
  <PARAM id="v0_cut" value="777.0"/>
  <PARAM id="v3_leg" value="1.0"/>
</MuToniState>)";
}

struct MuToniLayerCountPersistTests : juce::UnitTest
{
    MuToniLayerCountPersistTests() : juce::UnitTest("mu-Toni layer count and structural edits", "mu-toni-processor") {}

    static void loadSession(juce::AudioProcessor& p, const char* xml)
    {
        juce::MemoryBlock block;
        juce::AudioProcessor::copyXmlToBinary(*juce::XmlDocument::parse(xml), block);
        p.setStateInformation(block.getData(), (int) block.getSize());
    }

    // Gives each live layer a distinct cutoff and mixer level, so a move of the data can be seen.
    static void stamp(ExposedPluginProcessor& p)
    {
        for (int i = 0; i < p.getNumLayers(); ++i)
        {
            mu_seam_test::setParam(p.apvts, "a" + juce::String(i) + "_cut", 1000.0f + 100.0f * (float) i);
            mu_seam_test::setParam(p.apvts, "ch" + juce::String(i) + "_lvl", 0.1f + 0.1f * (float) i);
        }
    }

    // Layer `slot` carries the data that was stamped on layer `origin`, and the colour it had.
    void expectCarries(ExposedPluginProcessor& p, int slot, int origin, int originColour, const juce::String& what)
    {
        using namespace mu_seam_test;
        expectWithinAbsoluteError(getParam(p.apvts, "a" + juce::String(slot) + "_cut"), 1000.0f + 100.0f * (float) origin, 0.5f);
        expectWithinAbsoluteError(getParam(p.apvts, "ch" + juce::String(slot) + "_lvl"), 0.1f + 0.1f * (float) origin, 2e-3f);
        expectEquals(p.getLayerColourIndex(slot), originColour, what + ": colour of slot " + juce::String(slot));
    }

    void runTest() override
    {
        using namespace mu_seam_test;

        beginTest("a fresh instance has one layer, all eight Arps allocated, and layer 2 is audible at its defaults");
        {
            TempStorage storage;
            ExposedPluginProcessor p;
            expectEquals(p.numLayers.load(), 1);
            expectEquals(p.getNumLayers(), 1);
            for (int i = 0; i < mu_limits::kMaxLayers; ++i)
            {
                expect(p.layers[(size_t) i] != nullptr, "slot " + juce::String(i) + " is allocated");
                expectEquals(p.getLayerColourIndex(i), i, "slot " + juce::String(i) + " starts on palette colour " + juce::String(i));
            }
            expectWithinAbsoluteError(getParam(p.apvts, "a1_o1l"), 0.0f, 0.01f);   // not the old -60 dB
            expect(p.apvts.getParameter("a7_cut") != nullptr && p.apvts.getParameter("ch7_lvl") != nullptr, "layer 8 parameters exist");
        }

        beginTest("an old four-layer composed state loads as four layers");
        {
            TempStorage storage;
            ExposedPluginProcessor p;
            loadSession(p, kOldComposedState);
            expectEquals(p.numLayers.load(), 4);
            expectWithinAbsoluteError(getParam(p.apvts, "a0_cut"), 777.0f, 0.5f);
            expectEquals((int) getParam(p.apvts, "a3_leg"), 1);
            for (int i = 0; i < 4; ++i) expectEquals(p.getLayerColourIndex(i), i, "colours stay on the constructor allocation");
        }

        beginTest("an old APVTS dump loads as four layers");
        {
            TempStorage storage;
            ExposedPluginProcessor p;
            loadSession(p, kOldApvtsDump);
            expectEquals(p.numLayers.load(), 4);
            expectWithinAbsoluteError(getParam(p.apvts, "a0_cut"), 777.0f, 0.5f);
            expectEquals((int) getParam(p.apvts, "a3_leg"), 1);
        }

        beginTest("a six-layer state round-trips with its colours; the count is saved as numVoices, not a parameter");
        {
            TempStorage storage;
            ExposedPluginProcessor p;
            for (int i = 1; i < 6; ++i) expect(p.addLayer() == i, "addLayer appends");
            const int colours[6] = { 5, 3, 7, 0, 2, 6 };
            for (int i = 0; i < 6; ++i) p.layers[(size_t) i]->colourIndex = colours[i];
            stamp(p);

            juce::MemoryBlock block;
            p.getStateInformation(block);
            auto xml = juce::AudioProcessor::getXmlFromBinary(block.getData(), (int) block.getSize());
            expect(xml != nullptr, "the state decodes");
            if (xml == nullptr) return;
            expectEquals(xml->getIntAttribute("numVoices"), 6);
            expect(xml->getStringAttribute("voiceColours").startsWith("5,3,7,0,2,6"), "the colours are saved by slot");
            expect(p.apvts.getParameter("numLayers") == nullptr && p.apvts.getParameter("numVoices") == nullptr,
                   "the count is not an APVTS parameter");

            ExposedPluginProcessor q;
            q.setStateInformation(block.getData(), (int) block.getSize());
            expectEquals(q.numLayers.load(), 6);
            for (int i = 0; i < 6; ++i) expectCarries(q, i, i, colours[i], "round trip");
        }

        beginTest("fill to eight, remove the middle, swap: parameters, colour and mixer strip follow the layer");
        {
            TempStorage storage;
            ExposedPluginProcessor p;
            while (p.getNumLayers() < mu_limits::kMaxLayers) expect(p.addLayer() >= 0, "addLayer below the limit");
            expectEquals(p.addLayer(), -1, "addLayer at eight is refused");
            const int colours[8] = { 4, 1, 6, 2, 7, 0, 3, 5 };
            for (int i = 0; i < 8; ++i) p.layers[(size_t) i]->colourIndex = colours[i];
            stamp(p);

            // Remove layer 3: the ones above shift down one slot.
            expect(p.removeLayer(3), "removeLayer succeeds");
            expectEquals(p.getNumLayers(), 7);
            for (int i = 0; i < 3; ++i) expectCarries(p, i, i, colours[i], "after remove");
            for (int i = 3; i < 7; ++i) expectCarries(p, i, i + 1, colours[i + 1], "after remove");

            // Swap 0 and 5: each takes the other's data.
            const int c0 = p.getLayerColourIndex(0), c5 = p.getLayerColourIndex(5);
            expect(p.swapLayers(0, 5), "swapLayers succeeds");
            expectCarries(p, 0, 6, c5, "after swap");   // slot 5 held original layer 6
            expectCarries(p, 5, 0, c0, "after swap");
            expectEquals(p.getNumLayers(), 7);
            expect(! p.layerEditInFlight.load(), "no edit left the in-flight flag set");

            // A removed-then-re-added layer starts at defaults, not at the old tenant's data.
            expect(p.addLayer() == 7, "addLayer after remove");
            expectWithinAbsoluteError(getParam(p.apvts, "a7_cut"), 3000.0f, 0.5f);
        }
    }
};

static MuToniLayerCountPersistTests muToniLayerCountPersistTests;
