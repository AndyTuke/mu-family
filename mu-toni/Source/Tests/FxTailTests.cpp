// A full-preset commit mid-bar keeps the global FX tail, on the REAL mu-Toni PluginProcessor, built headless.
// (createEditor is defined in ProcessorSeamTests.cpp.)

#include "Plugin/PluginProcessor.h"
#include "Tests/FxTailChecks.h"

struct MuToniFxTailTests : juce::UnitTest
{
    MuToniFxTailTests() : juce::UnitTest("mu-Toni full-preset commit keeps the FX tail", "mu-toni-processor") {}

    void runTest() override
    {
        // A fresh layer already arps at its defaults once the transport runs.
        mu_fxtail_test::runFxTailChecks<mu_toni::PluginProcessor>(*this, [](mu_toni::PluginProcessor&) {});
    }
};

static MuToniFxTailTests muToniFxTailTests;
