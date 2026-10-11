// Tests on the REAL mu-On PluginProcessor, built headless (no editor, host or audio device):
// construction under a temp storage root and layer ownership after construction.

#include "Plugin/PluginProcessor.h"
#include "Tests/ProcessorSeamHelpers.h"

#include <set>

// The product class names its editor in a virtual, defined in PluginEditor.cpp, which this headless
// target does not link (see "Headless product processors" in design-plugin-family.md).
juce::AudioProcessorEditor* mu_on::PluginProcessor::createEditor() { return nullptr; }

namespace
{
    // The real processor with the layer storage exposed to the tests.
    struct ExposedPluginProcessor : mu_on::PluginProcessor
    {
        using mu_on::PluginProcessor::layers;
        using mu_on::PluginProcessor::numLayers;
    };
}

struct MuOnProcessorSeamTests : juce::UnitTest
{
    MuOnProcessorSeamTests() : juce::UnitTest("mu-On real PluginProcessor", "mu-on-processor") {}

    void runTest() override
    {
        using namespace mu_seam_test;
        beginTest("Five lanes exist after construction and each voiceSlot(i) is layer i");
        TempStorage storage;
        ExposedPluginProcessor p;
        expectEquals(p.numLayers.load(), (int) mu_on::kNumChannels);
        std::set<const void*> seen;
        for (int i = 0; i < mu_on::kNumChannels; ++i)
        {
            expect(p.layers[(size_t) i] != nullptr, "lane " + juce::String(i) + " is filled");
            expect(&p.voiceSlot(i) == p.layers[(size_t) i].get(), "voiceSlot(" + juce::String(i) + ") is layer " + juce::String(i));
            expect(p.getLayer(i) == p.layers[(size_t) i].get(), "getLayer(" + juce::String(i) + ") is layer " + juce::String(i));
            seen.insert(&p.voiceSlot(i));
        }
        expectEquals((int) seen.size(), (int) mu_on::kNumChannels, "five distinct tracks");

        beginTest("laneOf(voiceSlot(i)) == i for every lane (seen through the Track persistence hook)");
        for (int i = 0; i < mu_on::kNumChannels; ++i)
        {
            // Track::writeExtras picks its node from laneOf: a step row <Track i> for lanes 0-3,
            // the <RumbleEnv> for the Rumble lane.
            juce::ValueTree node("Slot");
            p.voiceSlot(i).writeExtras(node);
            if (i < mu_on::kNumStepLanes)
            {
                auto row = node.getChildWithName("Track");
                expect(row.isValid(), "lane " + juce::String(i) + " writes a step row");
                expectEquals((int) row.getProperty("i", -1), i, "lane " + juce::String(i) + " writes its own row");
            }
            else
                expect(node.getChildWithName("RumbleEnv").isValid(), "the Rumble lane writes its envelope");
        }

        beginTest("prepareToPlay and a processBlock run under the temp root");
        p.prepareToPlay(48000.0, 256);
        juce::AudioBuffer<float> buf(2, 256);
        juce::MidiBuffer midi;
        p.processBlock(buf, midi);
        expect(bufferIsSane(buf), "output is finite");
        expectEquals(p.getContentDir().getFullPathName(),
                     storage.root.getChildFile("content").getChildFile("muOn").getFullPathName());
    }
};

static MuOnProcessorSeamTests muOnProcessorSeamTests;
