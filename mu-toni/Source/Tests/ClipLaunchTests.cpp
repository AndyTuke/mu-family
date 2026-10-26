// Clip store / launch on the REAL mu-Toni PluginProcessor, built headless. (createEditor is defined
// in ProcessorSeamTests.cpp.)

#include "Plugin/PluginProcessor.h"
#include "Tests/ClipAutoSaveChecks.h"

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
        const mu_clip_test::Accessor<ExposedPluginProcessor> accessor {
            [](ExposedPluginProcessor& p, float v) { setParam(p.apvts, "a0_cut", v); },
            [](ExposedPluginProcessor& p) { return getParam(p.apvts, "a0_cut"); },
            1000.0f, 3000.0f, 1.0f };
        mu_clip_test::runClipLaunchChecks<ExposedPluginProcessor>(*this, accessor);
        mu_clip_test::runRestoreCancelsStagedSwapChecks<ExposedPluginProcessor>(*this, accessor);   // a host restore drops staged swaps
        mu_clip_test::runCaptureIdempotenceChecks<ExposedPluginProcessor>(*this, accessor, "mu-Toni");   // the auto-save poll rests on it
        mu_clip_test::runAutoSaveChecks<ExposedPluginProcessor>(*this, accessor, "mu-Toni");
    }
};

static MuToniClipLaunchTests muToniClipLaunchTests;
