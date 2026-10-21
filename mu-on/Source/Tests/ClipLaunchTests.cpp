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

        // The step row is not a parameter: a grid edit writes the track's row directly, so each clip must carry its own.
        for (const bool playing : { false, true })
        {
            beginTest(juce::String("clips hold the step edits made before each store; switching 1, 2, 1 swaps the live row (")
                      + (playing ? "playing" : "stopped") + ")");
            TempStorage storage;
            ExposedPluginProcessor p;
            auto& row = p.getTrack(0).row;
            const auto pattern = [&row]
            {
                juce::String s;
                for (int i = 0; i < mu_on::StepRow::kNumSteps; ++i) s += row.isOn(i) ? '1' : '0';
                return s;
            };
            const auto edit = [&row](int step) { row.setOn(step, ! row.isOn(step)); };   // the grid's left click

            edit(1); edit(5);
            const auto first = pattern();
            expect(p.storeClip(0, 0));
            edit(1); edit(2); edit(9);
            const auto second = pattern();
            expect(first != second);
            expect(p.storeClip(0, 1));

            const auto go = [&](int clip, const juce::String& expected)
            {
                if (playing) p.layerSwapper.flagBoundaries(true, 0.0, 0.1);
                expect(p.launchClip(0, clip));
                if (playing)
                {
                    expectEquals(pattern(), clip == 0 ? second : first, "the old row plays until the boundary");
                    expect(p.layerSwapper.flagBoundaries(true, 3.95, 0.2));
                    p.layerSwapper.commit();
                }
                expectEquals(pattern(), expected, "clip " + juce::String(clip + 1) + " row is live");
                expect(p.captureLayerPresetTree(0).getChildWithName("Track").getProperty("on").toString() == expected);
            };
            go(0, first); go(1, second); go(0, first);
        }

        beginTest("a clip carries its engine: launching it onto a track running another engine switches it back");
        {
            TempStorage storage;
            ExposedPluginProcessor p;
            p.addLayer();
            p.selectTrackEngine(1, mu_on::EngineType::Snare);
            setParam(p.apvts, "t1_snare_dec", 333.0f);
            expect(p.storeClip(1, 0));
            expectEquals(p.getLayer(1)->clips->get(0).getProperty("engine").toString(), p.getLayerName(1));

            p.selectTrackEngine(1, mu_on::EngineType::Hat);
            expectEquals((int) getParam(p.apvts, "t1_eng"), 2, "the Hat engine is selected");
            expect(p.launchClip(1, 0));
            expectEquals((int) getParam(p.apvts, "t1_eng"), 3, "the clip's Snare engine came back");
            expectWithinAbsoluteError(getParam(p.apvts, "t1_snare_dec"), 333.0f, 0.5f);
        }
    }
};

static MuOnClipLaunchTests muOnClipLaunchTests;
