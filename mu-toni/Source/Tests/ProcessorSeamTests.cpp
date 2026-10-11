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
    };
}

struct MuToniProcessorSeamTests : juce::UnitTest
{
    MuToniProcessorSeamTests() : juce::UnitTest("mu-Toni real PluginProcessor", "mu-toni-processor") {}

    void runTest() override
    {
        using namespace mu_seam_test;
        beginTest("Layers exist after construction and getLayer(i) is getArp(i)");
        TempStorage storage;
        ExposedPluginProcessor p;
        const int n = p.numLayers.load();
        expect(n >= 1, "at least one layer");
        std::set<const void*> seen;
        for (int i = 0; i < juce::jmin(n, 4); ++i)
        {
            expect(p.layers[(size_t) i] != nullptr, "layer " + juce::String(i) + " is filled");
            expect(p.getLayer(i) == &p.getArp(i), "getLayer(" + juce::String(i) + ") is getArp(" + juce::String(i) + ")");
            expect(&p.getArp(i) == p.layers[(size_t) i].get(), "getArp(" + juce::String(i) + ") is layer " + juce::String(i));
            seen.insert(&p.getArp(i));
        }
        expectEquals((int) seen.size(), juce::jmin(n, 4), "distinct arps");

        beginTest("prepareToPlay and a processBlock run under the temp root");
        p.prepareToPlay(48000.0, 256);
        juce::AudioBuffer<float> buf(2, 256);
        juce::MidiBuffer midi;
        p.processBlock(buf, midi);
        expect(bufferIsSane(buf), "output is finite");
        expectEquals(p.getContentDir().getFullPathName(),
                     storage.root.getChildFile("content").getChildFile("muToni").getFullPathName());
    }
};

static MuToniProcessorSeamTests muToniProcessorSeamTests;
