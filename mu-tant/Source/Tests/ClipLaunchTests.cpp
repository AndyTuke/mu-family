// Clip store / launch on the REAL mu-Tant PluginProcessor, built headless. (createEditor is defined
// in ProcessorSeamTests.cpp.)

#include "Plugin/PluginProcessor.h"
#include "Tests/ClipLaunchChecks.h"

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
        const auto id = [](int v) { return mu_tant::PluginProcessor::voiceParamId(v, "flt_res"); };
        mu_clip_test::runClipLaunchChecks<ExposedPluginProcessor>(
            *this, { [id](ExposedPluginProcessor& p, float v) { setParam(p.apvts, id(0), v); },
                     [id](ExposedPluginProcessor& p) { return getParam(p.apvts, id(0)); },
                     0.2f, 0.8f, 0.02f });
    }
};

static MuTantClipLaunchTests muTantClipLaunchTests;
