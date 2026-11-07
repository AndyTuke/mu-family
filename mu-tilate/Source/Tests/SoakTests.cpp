// A random-parameter / MIDI / state-restore soak of the real mu-tilate processor while the audio thread renders (set MU_SOAK_SECONDS to
// run it longer; the default is a few seconds). See mu-core/Tests/ParamSoak.h.

#include "Plugin/PluginProcessor.h"
#include "Tests/ParamSoak.h"

struct MuTilateSoakTests : juce::UnitTest
{
    MuTilateSoakTests() : juce::UnitTest("mu-tilate parameter soak", "mu-tilate-soak") {}

    void runTest() override
    {
        beginTest("random parameters, notes and state restores against a rendering audio thread");
        mu_seam_test::TempStorage storage;
        mu_tilate::PluginProcessor p;
        mu_seam_test::SoakOptions o;
        // A loop in layer 0 (detected and laid out) so the slice player, the stretcher and the timeline render real audio.
        o.setup = [&p, &storage]
        {
            p.loadLoopAsync(0, mu_seam_test::writeBurstLoop(storage.root));
            mu_seam_test::pumpMessages(3000);
            p.layOutSlices(0);
            mu_seam_test::pumpMessages(300);
        };
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

static MuTilateSoakTests muTilateSoakTests;
