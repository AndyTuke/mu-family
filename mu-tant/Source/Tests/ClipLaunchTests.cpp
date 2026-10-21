// Clip store / launch on the REAL mu-Tant PluginProcessor, built headless. (createEditor is defined
// in ProcessorSeamTests.cpp.)

#include "Plugin/PluginProcessor.h"
#include "Tests/ClipAutoSaveChecks.h"

namespace
{
    struct ExposedPluginProcessor : mu_tant::PluginProcessor
    {
        using mu_tant::PluginProcessor::layerSwapper;
    };
}

struct MuTantClipLaunchTests : juce::UnitTest
{
    MuTantClipLaunchTests() : juce::UnitTest("mu-Tant clip launch", "mu-tant") {}

    void runTest() override
    {
        using namespace mu_seam_test;
        const auto id = [](int v) { return mu_tant::layerParamId(v, "flt_res"); };
        const mu_clip_test::Accessor<ExposedPluginProcessor> accessor {
            [id](ExposedPluginProcessor& p, float v) { setParam(p.apvts, id(0), v); },
            [id](ExposedPluginProcessor& p) { return getParam(p.apvts, id(0)); },
            0.2f, 0.8f, 0.02f };
        mu_clip_test::runClipLaunchChecks<ExposedPluginProcessor>(*this, accessor);
        mu_clip_test::runRestoreCancelsStagedSwapChecks<ExposedPluginProcessor>(*this, accessor);   // a host restore drops staged swaps
        mu_clip_test::runCaptureIdempotenceChecks<ExposedPluginProcessor>(*this, accessor, "mu-Tant");   // the auto-save poll rests on it
        mu_clip_test::runAutoSaveChecks<ExposedPluginProcessor>(*this, accessor, "mu-Tant");
    }
};

static MuTantClipLaunchTests muTantClipLaunchTests;
