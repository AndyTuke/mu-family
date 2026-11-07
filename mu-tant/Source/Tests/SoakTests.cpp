// A random-parameter / MIDI / state-restore soak of the real mu-tant processor while the audio thread renders (set MU_SOAK_SECONDS to
// run it longer; the default is a few seconds). See mu-core/Tests/ParamSoak.h.

#include "Plugin/PluginProcessor.h"
#include "Tests/ParamSoak.h"

struct MuTantSoakTests : juce::UnitTest
{
    MuTantSoakTests() : juce::UnitTest("mu-tant parameter soak", "mu-tant-soak") {}

    void runTest() override
    {
        beginTest("random parameters, notes and state restores against a rendering audio thread");
        mu_seam_test::TempStorage storage;
        mu_tant::PluginProcessor p;
        mu_seam_test::SoakOptions o;
        o.structural = [&p](std::mt19937& rng)
        {
            const int n = p.getNumLayers();
            switch (rng() % 3)
            {
                case 0: if (n < mu_limits::kMaxLayers) p.addLayer(); break;
                case 1: if (n > 1) p.removeLayer((int) (rng() % (unsigned) n)); break;
                default: if (n > 1) p.swapLayers((int) (rng() % (unsigned) n), (int) (rng() % (unsigned) n)); break;
            }
        };
        mu_seam_test::runParamSoak(*this, p, o);
    }
};

static MuTantSoakTests muTantSoakTests;
