// Layer-extraction safety net for mu-On: the frozen parameter manifest (tests/manifests/mu-on.txt) and
// the compat corpus of real preset files (tests/corpus/mu-on/), both run through the real headless
// PluginProcessor. A deliberate change to either goes through MU_REGENERATE_FROZEN=1 (see
// tests/scripts/regenerate-frozen-baselines.ps1); a normal run never rewrites them.

#include "Plugin/PluginProcessor.h"
#include "Tests/FrozenBaselineHarness.h"

struct MuOnFrozenBaselineTests : juce::UnitTest
{
    MuOnFrozenBaselineTests() : juce::UnitTest("mu-On frozen parameter manifest and compat corpus", "mu-on-frozen") {}

    void runTest() override
    {
        mu_frozen::runManifestTest<mu_on::PluginProcessor>(*this, "mu-on", __FILE__);
        mu_frozen::runCorpusTests<mu_on::PluginProcessor>(*this, "mu-on", "muOn", "muTrack", __FILE__);
    }
};

static MuOnFrozenBaselineTests muFrozenBaselineTests;