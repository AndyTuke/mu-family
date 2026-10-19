// Layer-extraction safety net for mu-Tant: the frozen parameter manifest (tests/manifests/mu-tant.txt) and
// the compat corpus of real preset files (tests/corpus/mu-tant/), both run through the real headless
// PluginProcessor. A deliberate change to either goes through MU_REGENERATE_FROZEN=1 (see
// tests/scripts/regenerate-frozen-baselines.ps1); a normal run never rewrites them.

#include "Plugin/PluginProcessor.h"
#include "Tests/FrozenBaselineHarness.h"

struct MuTantFrozenBaselineTests : juce::UnitTest
{
    MuTantFrozenBaselineTests() : juce::UnitTest("mu-Tant frozen parameter manifest and compat corpus", "mu-tant-frozen") {}

    void runTest() override
    {
        mu_frozen::runManifestTest<mu_tant::PluginProcessor>(*this, "mu-tant", __FILE__);
        mu_frozen::runCorpusTests<mu_tant::PluginProcessor>(*this, "mu-tant", "muTant", "muPattern", __FILE__);
    }
};

static MuTantFrozenBaselineTests muFrozenBaselineTests;