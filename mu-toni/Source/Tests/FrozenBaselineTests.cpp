// Layer-extraction safety net for mu-Toni: the frozen parameter manifest (tests/manifests/mu-toni.txt) and
// the compat corpus of real preset files (tests/corpus/mu-toni/), both run through the real headless
// PluginProcessor. A deliberate change to either goes through MU_REGENERATE_FROZEN=1 (see
// tests/scripts/regenerate-frozen-baselines.ps1); a normal run never rewrites them.

#include "Plugin/PluginProcessor.h"
#include "Tests/FrozenBaselineHarness.h"

struct MuToniFrozenBaselineTests : juce::UnitTest
{
    MuToniFrozenBaselineTests() : juce::UnitTest("mu-Toni frozen parameter manifest and compat corpus", "mu-toni-frozen") {}

    void runTest() override
    {
        mu_frozen::runManifestTest<mu_toni::PluginProcessor>(*this, "mu-toni", __FILE__);
        mu_frozen::runCorpusTests<mu_toni::PluginProcessor>(*this, "mu-toni", "muToni", "muArp", __FILE__);
    }
};

static MuToniFrozenBaselineTests muFrozenBaselineTests;