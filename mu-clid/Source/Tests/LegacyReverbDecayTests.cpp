// mu-Clid presets and sessions saved before the reverb Decay knob load with the Decay their reverb had
// then, never the Decay the instance held before (mu-Clid leaves a global the file lacks where it was, so
// an old preset used to inherit the previous one's Decay). The corpus check is shared
// (mu-core/Tests/LegacyReverbDecayCorpus.h); the older APVTS-dump session format is checked here.

#include "Plugin/PluginProcessor.h"
#include "Tests/LegacyReverbDecayCorpus.h"

struct MuClidLegacyReverbDecayTests : juce::UnitTest
{
    MuClidLegacyReverbDecayTests() : juce::UnitTest("mu-Clid old presets keep their reverb Decay", "mu-clid-processor") {}

    static void setPlain(mu_clid::PluginProcessor& p, const char* id, float v)
    {
        auto* prm = p.apvts.getParameter(id);
        prm->setValueNotifyingHost(prm->convertTo0to1(v));
    }

    void runTest() override
    {
        mu_legacy_reverb::runCorpusDecayTests<mu_clid::PluginProcessor>(*this, "mu-clid", "muClid", __FILE__);

        beginTest("an APVTS-dump session without rev_decay: the saved algorithm's old Decay, else the default one's");
        for (const bool withAlgo : { true, false })
        {
            mu_seam_test::TempStorage storage;
            mu_clid::PluginProcessor source;
            auto dump = source.apvts.copyState();
            dump.removeChild(dump.getChildWithProperty("id", mu_pp::kReverbDecayId), nullptr);
            auto algo = dump.getChildWithProperty("id", mu_pp::kReverbAlgoId);
            if (withAlgo) algo.setProperty("value", 2, nullptr);   // Plate
            else          dump.removeChild(algo, nullptr);
            const auto xml = dump.toXmlString();

            mu_clid::PluginProcessor p;
            setPlain(p, mu_pp::kReverbAlgoId, 3.0f);   // Spring: replaceState resets it to the default when the dump has no rev_algo
            setPlain(p, mu_pp::kReverbDecayId, 9.0f);
            p.setStateInformation(xml.toRawUTF8(), (int) xml.getNumBytesAsUTF8());
            p.flushPendingAsyncUpdates();

            const int expected = withAlgo ? 2 : 1;
            const float decay = p.apvts.getRawParameterValue(mu_pp::kReverbDecayId)->load();
            logMessage(juce::String("dump ") + (withAlgo ? "with" : "without") + " rev_algo: algorithm "
                       + juce::String(p.apvts.getRawParameterValue(mu_pp::kReverbAlgoId)->load()) + " (slot "
                       + juce::String(p.fxChain.reverbSlot().getAlgorithmIndex()) + "), decay " + juce::String(decay, 9));
            expectEquals(p.fxChain.reverbSlot().getAlgorithmIndex(), expected, "the algorithm");
            expect(std::abs(decay - mu_pp::legacyReverbDecay(expected)) <= 1.0e-5f,
                   "Decay " + juce::String(decay, 9) + " is the algorithm's old " + juce::String(mu_pp::legacyReverbDecay(expected), 9));
            expect(p.fxChain.reverbSlot().getDecay() == decay, "and reaches the slot");
        }
    }
};

static MuClidLegacyReverbDecayTests muClidLegacyReverbDecayTests;
