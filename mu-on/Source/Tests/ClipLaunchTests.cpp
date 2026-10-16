// Clip store / launch on the REAL mu-On PluginProcessor, built headless: the shared checks, plus a
// clip carrying its engine. (createEditor is defined in ProcessorSeamTests.cpp.)

#include "Plugin/PluginProcessor.h"
#include "Tests/ClipLaunchChecks.h"

namespace
{
    struct ExposedPluginProcessor : mu_on::PluginProcessor
    {
        using mu_on::PluginProcessor::layerSwapper;
    };
}

struct MuOnClipLaunchTests : juce::UnitTest
{
    MuOnClipLaunchTests() : juce::UnitTest("mu-On clip launch", "mu-on") {}

    void runTest() override
    {
        using namespace mu_seam_test;
        const mu_clip_test::Accessor<ExposedPluginProcessor> accessor {
            [](ExposedPluginProcessor& p, float v) { setParam(p.apvts, "t0_kick_tune", v); },
            [](ExposedPluginProcessor& p) { return getParam(p.apvts, "t0_kick_tune"); },
            40.0f, 80.0f, 0.2f };
        mu_clip_test::runClipLaunchChecks<ExposedPluginProcessor>(*this, accessor);
        mu_clip_test::runRestoreCancelsStagedSwapChecks<ExposedPluginProcessor>(*this, accessor);   // a host restore drops staged swaps

        beginTest("a clip carries its engine: launching it onto a track running another engine switches it back");
        {
            TempStorage storage;
            ExposedPluginProcessor p;
            p.addLayer();
            p.selectEngine(1, mu_on::EngineType::Snare);
            setParam(p.apvts, "t1_snare_dec", 333.0f);
            expect(p.storeClip(1, 0));
            expectEquals(p.getLayer(1)->clips->get(0).getProperty("engine").toString(), p.getLayerName(1));

            p.selectEngine(1, mu_on::EngineType::Hat);
            expectEquals((int) getParam(p.apvts, "t1_eng"), 2, "the Hat engine is selected");
            expect(p.launchClip(1, 0));
            expectEquals((int) getParam(p.apvts, "t1_eng"), 3, "the clip's Snare engine came back");
            expectWithinAbsoluteError(getParam(p.apvts, "t1_snare_dec"), 333.0f, 0.5f);
        }
    }
};

static MuOnClipLaunchTests muOnClipLaunchTests;
