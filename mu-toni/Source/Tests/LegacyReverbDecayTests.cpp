// mu-Toni presets and sessions saved before the reverb Decay knob load with the Decay their reverb had
// then (the shared check in mu-core/Tests/LegacyReverbDecayCorpus.h, on the real processor).

#include "Plugin/PluginProcessor.h"
#include "Tests/LegacyReverbDecayCorpus.h"

struct MuToniLegacyReverbDecayTests : juce::UnitTest
{
    MuToniLegacyReverbDecayTests() : juce::UnitTest("mu-Toni old presets keep their reverb Decay", "mu-toni-processor") {}

    void runTest() override
    {
        mu_legacy_reverb::runCorpusDecayTests<mu_toni::PluginProcessor>(*this, "mu-toni", "muToni", __FILE__);
    }
};

static MuToniLegacyReverbDecayTests muToniLegacyReverbDecayTests;
