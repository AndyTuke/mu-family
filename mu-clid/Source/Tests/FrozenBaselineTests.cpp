// Layer-extraction safety net for mu-Clid: the frozen parameter manifest (tests/manifests/mu-clid.txt) and
// the compat corpus of real preset files (tests/corpus/mu-clid/), both run through the real headless
// PluginProcessor. A deliberate change to either goes through MU_REGENERATE_FROZEN=1 (see
// tests/scripts/regenerate-frozen-baselines.ps1); a normal run never rewrites them.

#include "Plugin/PluginProcessor.h"
#include "Tests/FrozenBaselineHarness.h"

struct MuClidFrozenBaselineTests : juce::UnitTest
{
    MuClidFrozenBaselineTests() : juce::UnitTest("mu-Clid frozen parameter manifest and compat corpus", "mu-clid-frozen") {}

    void runTest() override
    {
        mu_frozen::runManifestTest<mu_clid::PluginProcessor>(*this, "mu-clid", __FILE__);
        mu_frozen::runCorpusTests<mu_clid::PluginProcessor>(*this, "mu-clid", "muClid", "muRhythm", __FILE__);
    }
};

static MuClidFrozenBaselineTests muFrozenBaselineTests;