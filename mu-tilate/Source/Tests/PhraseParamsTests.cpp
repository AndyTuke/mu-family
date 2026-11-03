// The Phrase parameter table: every row the suffix table names exists under the slot's prefix (and only there),
// the rows' defaults are what the design says, and every modulation target names a real row with a proportional depth.

#include "Plugin/MixerFxParams.h"   // mu-core: addChannelStripParams
#include "Modulation/ModTargetChecks.h"   // mu-core: missingParams, depthNotProportional
#include "Modulation/MuTilateModDest.h"
#include "Persistence/PhraseParams.h"
#include "Sequencer/Timeline.h"
#include "Tests/ParamHost.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

using namespace mu_tilate;

struct PhraseParamsTests : public juce::UnitTest
{
    PhraseParamsTests() : juce::UnitTest("Phrase parameters", "mu-tilate") {}

    void runTest() override
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        addLayerParams(layout, 0);
        addLayerParams(layout, 3);
        ParamHost host(std::move(layout));
        auto& apvts = host.apvts;

        beginTest("every row exists under its slot's prefix, and the count matches the table");
        {
            for (int slot : { 0, 3 })
                for (int i = 0; i < kNumPhraseParams; ++i)
                    expect(apvts.getParameter(layerParamPrefix(slot) + ppi::suffix[i]) != nullptr, juce::String("missing ") + ppi::suffix[i]);
            expect(apvts.getParameter("p1_mode") == nullptr);
            int n = 0;
            for (auto* p : host.getParameters()) { juce::ignoreUnused(p); ++n; }
            expectEquals(n, 2 * kNumPhraseParams);
        }

        beginTest("defaults: tempo match on, 120 BPM, a 10 ms hold at full strength, 1/16 grid");
        {
            auto get = [&](int row) { return apvts.getParameter(layerParamPrefix(0) + ppi::suffix[row]); };
            expectEquals(get(ppi::mode)->getValue(), get(ppi::mode)->convertTo0to1(1.0f));
            expectWithinAbsoluteError(get(ppi::ltempo)->convertFrom0to1(get(ppi::ltempo)->getDefaultValue()), 120.0f, 1.0e-3f);
            expectWithinAbsoluteError(get(ppi::hold)->convertFrom0to1(get(ppi::hold)->getDefaultValue()), 10.0f, 1.0e-3f);
            expectWithinAbsoluteError(get(ppi::strength)->convertFrom0to1(get(ppi::strength)->getDefaultValue()), 1.0f, 1.0e-6f);
            expectEquals(gridBeats(3), 0.25);
            expectWithinAbsoluteError(gridBeats(6), 1.0 / 3.0, 1.0e-12);          // 1/8T: three in the time of a quarter note
            expectWithinAbsoluteError(Timeline::snap(0.7, gridBeats(6)), 2.0 / 3.0, 1.0e-12);
            expectEquals(gridBeats(99), gridBeats(8));
        }

        beginTest("every modulation target names a real row and its depth is a proportion of the range");
        {
            {
                // The layer's rows plus its mixer strip (the pan and send targets are the strip's parameters).
                juce::AudioProcessorValueTreeState::ParameterLayout withStrip;
                addLayerParams(withStrip, 0);
                mu_mixfx::addChannelStripParams(withStrip, "ch0_", "Layer 1 Ch ");
                ParamHost stripHost(std::move(withStrip));
                expectEquals(mu_mod::checks::missingParams(kModDestTable, stripHost.apvts, "p0_").size(), 0);
            }
            expectEquals(mu_mod::checks::depthNotProportional(kModDestTable).size(), 0);
            // Every continuous layer parameter is a target unless it is exempt here, with the reason.
            static constexpr mu_mod::checks::ModExemption exempt[] = {
                { "dsens", "detector setting: used when Detect runs, not per block" },
                { "dgap",  "detector setting: used when Detect runs, not per block" },
                { "dpre",  "detector setting: used when Detect runs, not per block" },
                { "dlow",  "detector setting: used when Detect runs, not per block" },
                { "dhigh", "detector setting: used when Detect runs, not per block" },
            };
            const auto unlisted = mu_mod::checks::unlistedContinuousParams(kModDestTable, exempt, apvts, "p0_");
            expect(unlisted.isEmpty(), "layer parameters that are not modulation targets: " + unlisted.joinIntoString(", "));
            expect(isValidModDest("stretch.hold"));
            expect(! isValidModDest("stretch.curve"));
        }
    }
};

static PhraseParamsTests phraseParamsTests;
