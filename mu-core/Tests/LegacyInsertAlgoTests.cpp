// A preset row saved before the format-2 rows holds an insert algorithm as a fraction of the algorithm list as it
// then stood (index / 13). It must still load as the same algorithm after the list grew, whereas a row that holds
// the actual index (x) or the name is read as it says.

#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "Persistence/LayerState.h"
#include "Audio/AlgorithmNames.h"

struct LegacyInsertAlgoTests : juce::UnitTest
{
    LegacyInsertAlgoTests() : juce::UnitTest("A legacy normalised insert-algorithm row keeps its algorithm", "mu-core") {}

    void runTest() override
    {
        juce::AudioParameterInt algo(juce::ParameterID { "v0_drvChar", 1 }, "Insert Algo", 0, mu_audio::kInsertAlgorithmCount - 1, 0);
        juce::AudioParameterFloat other(juce::ParameterID { "v0_level", 1 }, "Level", 0.0f, 1.0f, 0.5f);

        beginTest("each algorithm the list had when the fraction was stored reads back as itself");
        for (int index = 0; index <= mu_audio::kLegacyNormalisedInsertAlgoMax; ++index)
        {
            juce::ValueTree row(mu_pp::kRowTag);
            row.setProperty("id", "v0_drvChar", nullptr);
            row.setProperty("v", (double) index / (double) mu_audio::kLegacyNormalisedInsertAlgoMax, nullptr);
            float normalised = -1.0f;
            expect(mu_pp::readRowValue(row, algo, normalised));
            expectEquals(juce::roundToInt(algo.convertFrom0to1(normalised)), index, "legacy index " + juce::String(index));
        }

        beginTest("an actual-value row is read as the index it holds, including Overdrive");
        {
            juce::ValueTree row(mu_pp::kRowTag);
            row.setProperty("id", "v0_drvChar", nullptr);
            row.setProperty("x", (double) (mu_audio::kInsertAlgorithmCount - 1), nullptr);
            float normalised = -1.0f;
            expect(mu_pp::readRowValue(row, algo, normalised));
            expectEquals(juce::roundToInt(algo.convertFrom0to1(normalised)), mu_audio::kInsertAlgorithmCount - 1);
        }

        beginTest("a normalised row of any other parameter is read as a plain fraction");
        {
            juce::ValueTree row(mu_pp::kRowTag);
            row.setProperty("id", "v0_level", nullptr);
            row.setProperty("v", 0.25, nullptr);
            float normalised = -1.0f;
            expect(mu_pp::readRowValue(row, other, normalised));
            expectWithinAbsoluteError(normalised, 0.25f, 1.0e-6f);
        }

        beginTest("the insert menu lists every algorithm once, with None first, and follows the order column");
        {
            juce::Array<int> ids;
            juce::StringArray names;
            mu_audio::populateInsertAlgoDropdown([&] (const char* n, int id) { names.add(n); ids.add(id); });
            expectEquals(ids.size(), mu_audio::kInsertAlgorithmCount);
            for (int id = 1; id <= mu_audio::kInsertAlgorithmCount; ++id)
                expect(ids.contains(id), "id " + juce::String(id) + " is in the menu exactly once");
            expectEquals(names[0], juce::String("None"));
            expectEquals(ids[0], 1);

            // The order column drives the display: the shown order is ascending by it.
            int last = -1;
            for (int id : ids)
                for (const auto& e : mu_audio::kInsertMenu)
                    if (e.algoIndex + 1 == id) { expect(e.order > last, "ascending order"); last = e.order; }
        }
    }
};

static LegacyInsertAlgoTests legacyInsertAlgoTests;
