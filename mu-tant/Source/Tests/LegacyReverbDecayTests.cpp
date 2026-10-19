// mu-Tant presets and sessions saved before the reverb Decay knob load with the Decay their reverb had
// then (the shared check in mu-core/Tests/LegacyReverbDecayCorpus.h, on the real processor).

#include "Plugin/PluginProcessor.h"
#include "Tests/LegacyReverbDecayCorpus.h"

struct MuTantLegacyReverbDecayTests : juce::UnitTest
{
    MuTantLegacyReverbDecayTests() : juce::UnitTest("mu-Tant old presets keep their reverb Decay", "mu-tant-processor") {}

    void runTest() override
    {
        mu_legacy_reverb::runCorpusDecayTests<mu_tant::PluginProcessor>(*this, "mu-tant", "muTant", __FILE__);
    }
};

static MuTantLegacyReverbDecayTests muTantLegacyReverbDecayTests;
