// Algorithm name table sanity tests (#457).
//
// Asserts that each algorithm name table has the same number of entries as the
// corresponding dispatch-table constant. Catches the failure mode where someone
// adds a new algorithm to a dispatch table but forgets to append a stable name,
// causing the v2 preset writer to fall back to writing an integer index — which
// silently breaks the reorder-safety contract.
//
// Table → constant pairing:
//   kInsertAlgorithmNames  → InsertProcessor::kNumInsertAlgos  (13)
//   kFilterTypeNames       → MultiModeFilter::kNumFilterAlgos  (16)
//   kEffectAlgorithmNames  → FXAlgorithmRegistry::effectAlgorithms().size() (4)
//   kReverbAlgorithmNames  → FXAlgorithmRegistry::reverbAlgorithms().size() (4)

#include <juce_core/juce_core.h>
#include "../Audio/AlgorithmNames.h"
#include "../Audio/InsertProcessor.h"
#include "../Audio/MultiModeFilter.h"
#include "../Audio/FX/Slots/FXAlgorithmDef.h"

using mu_audio::countNames;
using mu_audio::kInsertAlgorithmNames;
using mu_audio::kFilterTypeNames;
using mu_audio::kEffectAlgorithmNames;
using mu_audio::kReverbAlgorithmNames;

class AlgorithmNameTableTest : public juce::UnitTest
{
public:
    AlgorithmNameTableTest() : juce::UnitTest ("Algorithm name table sizes", "Preset") {}

    void runTest() override
    {
        beginTest ("kInsertAlgorithmNames count matches InsertProcessor::kNumInsertAlgos");
        expectEquals (countNames (kInsertAlgorithmNames),
                      InsertProcessor::kNumInsertAlgos,
                      "kInsertAlgorithmNames has wrong number of entries");

        beginTest ("kFilterTypeNames count matches MultiModeFilter::kNumFilterAlgos");
        expectEquals (countNames (kFilterTypeNames),
                      MultiModeFilter::kNumFilterAlgos,
                      "kFilterTypeNames has wrong number of entries");

        beginTest ("kEffectAlgorithmNames count matches FXAlgorithmRegistry::effectAlgorithms().size()");
        expectEquals (countNames (kEffectAlgorithmNames),
                      (int) FXAlgorithmRegistry::effectAlgorithms().size(),
                      "kEffectAlgorithmNames has wrong number of entries");

        beginTest ("kReverbAlgorithmNames count matches FXAlgorithmRegistry::reverbAlgorithms().size()");
        expectEquals (countNames (kReverbAlgorithmNames),
                      (int) FXAlgorithmRegistry::reverbAlgorithms().size(),
                      "kReverbAlgorithmNames has wrong number of entries");

        beginTest ("All name table entries are non-empty ASCII strings without spaces");
        auto checkTable = [this](const char* const* table, const char* tableName)
        {
            for (int i = 0; table[i] != nullptr; ++i)
            {
                const juce::String name (table[i]);
                expect (name.isNotEmpty(),
                        juce::String(tableName) + "[" + juce::String(i) + "] is empty");
                expect (!name.containsAnyOf (" \t"),
                        juce::String(tableName) + "[" + juce::String(i) + "] contains whitespace: '" + name + "'");
            }
        };

        checkTable (kInsertAlgorithmNames, "kInsertAlgorithmNames");
        checkTable (kFilterTypeNames,      "kFilterTypeNames");
        checkTable (kEffectAlgorithmNames, "kEffectAlgorithmNames");
        checkTable (kReverbAlgorithmNames, "kReverbAlgorithmNames");
    }
};

static AlgorithmNameTableTest algorithmNameTableTest;
