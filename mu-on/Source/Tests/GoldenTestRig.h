#pragma once

// Shared by the mu-On golden-render scenarios and the extraction pins: a real headless PluginProcessor
// that owns its own temporary storage root and renders with deterministic noise, plus the few
// helpers both files use to set a track up.

#include "Plugin/PluginProcessor.h"
#include "Tests/ProcessorSeamHelpers.h"

namespace mu_golden_rig
{
    // Set up before the processor base is built (base classes construct in order): deterministic noise
    // for the processor's lifetime, and a private storage root so settings never leak between runs.
    struct StorageHolder
    {
        mu_seam_test::ScopedEnvVar deterministicNoise { "MU_DETERMINISTIC_NOISE", "1" };
        mu_seam_test::TempStorage  storage;
    };

    struct OwnedProcessor : StorageHolder, mu_on::PluginProcessor {};

    inline void setParam(juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float plain)
    {
        mu_seam_test::setParam(apvts, id, plain);
    }

    // Track `t`'s step row from two 16-character strings: '1' = on / accented.
    inline void setRow(mu_on::PluginProcessor& p, int t, const char* on, const char* accent = "0000000000000000")
    {
        for (int s = 0; s < 16; ++s)
        {
            p.getTrack(t).row.setOn(s, on[s] == '1');
            p.getTrack(t).row.setAccent(s, accent[s] == '1');
        }
    }

    // Grows the processor to at least `t + 1` tracks and sets track `t` to `engine`.
    inline void useTrack(mu_on::PluginProcessor& p, int t, mu_on::EngineType engine)
    {
        while (p.getNumLayers() <= t) p.addLayer();
        p.selectTrackEngine(t, engine);
    }
}
