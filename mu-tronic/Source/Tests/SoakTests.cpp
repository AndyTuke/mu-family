// A random-parameter / MIDI / state-restore soak of the real mu-tronic processor while the audio thread renders (set MU_SOAK_SECONDS to
// run it longer; the default is a few seconds). See mu-core/Tests/ParamSoak.h.

#include "Plugin/PluginProcessor.h"
#include "Tests/ParamSoak.h"
#include "Sequencer/PhraseSlotHost.h"

struct MuTronicSoakTests : juce::UnitTest
{
    MuTronicSoakTests() : juce::UnitTest("mu-tronic parameter soak", "mu-tronic-soak") {}

    void runTest() override
    {
        beginTest("random parameters, notes and state restores against a rendering audio thread");
        mu_seam_test::TempStorage storage;
        mu_tronic::PluginProcessor p;
        mu_seam_test::SoakOptions o;
        // Slot 0 is a Phrase holding a loop, so the slice player renders real audio while types change around it.
        o.setup = [&p, &storage]
        {
            p.setLayerType(0, mu_core::LayerTypeId::Phrase);
            if (auto* host = dynamic_cast<mu_tilate::PhraseSlotHost*>(p.findLayerServices(mu_core::LayerTypeId::Phrase)))
            {
                host->loadLoopAsync(0, mu_seam_test::writeBurstLoop(storage.root));
                mu_seam_test::pumpMessages(3000);
                host->layOutSlices(0);
                mu_seam_test::pumpMessages(300);
            }
        };
        // Every slot is a place for any registered type: change a random slot to a random type (Empty included).
        o.structural = [&p](std::mt19937& rng)
        {
            static const mu_core::LayerTypeId types[] = { mu_core::LayerTypeId::Empty, mu_core::LayerTypeId::Rhythm, mu_core::LayerTypeId::Pattern,
                mu_core::LayerTypeId::Arp, mu_core::LayerTypeId::Kick, mu_core::LayerTypeId::Bass, mu_core::LayerTypeId::Hat,
                mu_core::LayerTypeId::Snare, mu_core::LayerTypeId::Rumble, mu_core::LayerTypeId::FmPerc, mu_core::LayerTypeId::Chord, mu_core::LayerTypeId::Phrase };
            p.setLayerType((int) (rng() % (unsigned) mu_limits::kMaxLayers), types[rng() % (sizeof(types) / sizeof(types[0]))]);
        };
        mu_seam_test::runParamSoak(*this, p, o);
    }
};

static MuTronicSoakTests muTronicSoakTests;
