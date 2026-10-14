// Clip store / launch on the REAL mu-Toni PluginProcessor, built headless. (createEditor is defined
// in ProcessorSeamTests.cpp.)

#include "Plugin/PluginProcessor.h"
#include "Tests/ClipLaunchChecks.h"

namespace
{
    struct ExposedPluginProcessor : mu_toni::PluginProcessor
    {
        using mu_toni::PluginProcessor::layerSwapper;
    };
}

struct MuToniClipLaunchTests : juce::UnitTest
{
    MuToniClipLaunchTests() : juce::UnitTest("mu-Toni clip launch", "mu-toni") {}

    void runTest() override
    {
        using namespace mu_seam_test;
        mu_clip_test::runClipLaunchChecks<ExposedPluginProcessor>(
            *this, { [](ExposedPluginProcessor& p, float v) { setParam(p.apvts, "a0_cut", v); },
                     [](ExposedPluginProcessor& p) { return getParam(p.apvts, "a0_cut"); },
                     1000.0f, 3000.0f, 1.0f });
    }
};

static MuToniClipLaunchTests muToniClipLaunchTests;
