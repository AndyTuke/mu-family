// mu-On presets and sessions saved before the reverb Decay knob load with the Decay their reverb had
// then (the shared check in mu-core/Tests/LegacyReverbDecayCorpus.h, on the real processor).

#include "Plugin/PluginProcessor.h"
#include "Tests/LegacyReverbDecayCorpus.h"

struct MuOnLegacyReverbDecayTests : juce::UnitTest
{
    MuOnLegacyReverbDecayTests() : juce::UnitTest("mu-On old presets keep their reverb Decay", "mu-on-processor") {}

    void runTest() override
    {
        mu_legacy_reverb::runCorpusDecayTests<mu_on::PluginProcessor>(*this, "mu-on", "muOn", __FILE__);
    }
};

static MuOnLegacyReverbDecayTests muOnLegacyReverbDecayTests;
