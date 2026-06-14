// APVTS parameter layout uniqueness tests.
//
// Duplicate APVTS parameter IDs silently bind both parameters to the same
// value - the second registration wins, making the first permanently broken.
// This test catches suffix/ID collisions at the table level, where it's cheap
// to check.  The kGlobalParamDefs / kGlobalParams cross-check lives in
// GlobalParamDefsTests.cpp.

#include <juce_core/juce_core.h>
#include <unordered_map>
#include "../Persistence/RhythmParamTable.h"
#include "../Persistence/PresetHelpers.h"

class ApvtsLayoutTest : public juce::UnitTest
{
public:
    ApvtsLayoutTest() : juce::UnitTest ("APVTS layout uniqueness", "Preset") {}

    void runTest() override
    {
        // ── kRhythmParamDefs suffix uniqueness ────────────────────────────────
        beginTest ("kRhythmParamDefs - all suffixes are unique");
        {
            std::unordered_map<std::string, int> seen;
            for (int i = 0; i < mu_pp::kRhythmParamCount; ++i)
            {
                const std::string s = mu_pp::kRhythmParamDefs[i].suffix;
                auto it = seen.find(s);
                if (it != seen.end())
                    expect (false, "Duplicate rhythm param suffix '" + s + "' at indices "
                          + std::to_string(it->second) + " and " + std::to_string(i));
                seen[s] = i;
            }
            expectEquals ((int)seen.size(), mu_pp::kRhythmParamCount,
                "Expected " + juce::String(mu_pp::kRhythmParamCount) + " unique suffixes");
        }

        // ── kRhythmParamDefs - no empty suffixes ──────────────────────────────
        beginTest ("kRhythmParamDefs - no entry has an empty suffix");
        {
            for (int i = 0; i < mu_pp::kRhythmParamCount; ++i)
            {
                const juce::String s (mu_pp::kRhythmParamDefs[i].suffix);
                expect (s.isNotEmpty(),
                        "Empty suffix at kRhythmParamDefs[" + juce::String(i) + "]");
            }
        }

        // ── kGlobalParamDefs ID uniqueness ────────────────────────────────────
        beginTest ("kGlobalParamDefs - all IDs are unique");
        {
            std::unordered_map<std::string, int> seen;
            for (int i = 0; i < mu_pp::kGlobalParamDefCount; ++i)
            {
                const std::string s = mu_pp::kGlobalParamDefs[i].id;
                auto it = seen.find(s);
                if (it != seen.end())
                    expect (false, "Duplicate global param ID '" + s + "' at indices "
                          + std::to_string(it->second) + " and " + std::to_string(i));
                seen[s] = i;
            }
            expectEquals ((int)seen.size(), mu_pp::kGlobalParamDefCount);
        }

        // ── kRhythmParamDefs - AlgorithmIndex entries have non-null tables ────
        beginTest ("AlgorithmIndex params each have a non-null algorithm name table");
        {
            for (int i = 0; i < mu_pp::kRhythmParamCount; ++i)
            {
                const auto& def = mu_pp::kRhythmParamDefs[i];
                if (def.kind == mu_pp::ParamKind::AlgorithmIndex)
                    expect (def.algorithmNames != nullptr,
                            juce::String("AlgorithmIndex param '") + def.suffix
                            + "' has null name table");
            }
        }
    }
};

static ApvtsLayoutTest apvtsLayoutTest;
