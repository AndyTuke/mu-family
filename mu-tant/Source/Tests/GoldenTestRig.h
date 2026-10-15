#pragma once

// Shared by the mu-Tant golden-render scenarios and the extraction pins: a real headless PluginProcessor
// that owns its own temporary storage root and renders with deterministic noise, plus the few helpers
// both files use to set a voice up. Mirrors mu-on/Source/Tests/GoldenTestRig.h (a product's rig names
// its own processor class, so each product carries its copy).

#include "Plugin/PluginProcessor.h"
#include "Tests/ProcessorSeamHelpers.h"

#include <vector>

namespace mu_golden_rig
{
    // Set up before the processor base is built (base classes construct in order): deterministic noise
    // for the processor's lifetime, and a private storage root so settings never leak between runs.
    struct StorageHolder
    {
        mu_seam_test::ScopedEnvVar deterministicNoise { "MU_DETERMINISTIC_NOISE", "1" };
        mu_seam_test::TempStorage  storage;
    };

    // The processor the tests drive. drainDeferredWork is the message thread's hot-swap poll, run by the
    // test between blocks (the console tests pump no timers, so a staged swap commits only when drained);
    // `fired` is the scenario's record of which timed actions have run (a fresh processor per stream).
    struct OwnedProcessor : StorageHolder, mu_tant::PluginProcessor
    {
        using mu_tant::PluginProcessor::drainDeferredWork;
        std::vector<bool> fired;
    };

    inline juce::String vp(int voice, const char* base) { return mu_tant::PluginProcessor::voiceParamId(voice, base); }

    inline void setParam(juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float plain)
    {
        mu_seam_test::setParam(apvts, id, plain);
    }

    // Sets a voice parameter by its short id ("o1_oct", "flt_cut", ...).
    inline void setVoice(juce::AudioProcessorValueTreeState& apvts, int voice, const char* base, float plain)
    {
        mu_seam_test::setParam(apvts, vp(voice, base), plain);
    }

    // Opens the gater on a voice so its drone reaches the output whatever the transport does.
    inline void openGate(juce::AudioProcessorValueTreeState& apvts, int voice) { setVoice(apvts, voice, "gate_bypass", 1.0f); }

    // Grows the processor to at least `n` voices.
    inline void growTo(mu_tant::PluginProcessor& p, int n)
    {
        while (p.getNumLayers() < n) p.addLayer();
    }
}
