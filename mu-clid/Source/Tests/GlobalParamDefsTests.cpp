// kGlobalParamDefs round-trip test.
//
// For every entry in kGlobalParamDefs, picks a representative value, writes it
// into a ValueTree via writeKindedProperty, reads it back via
// readKindedPropertyAsActualV2, and asserts the round-trip matches.

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include "../Persistence/PresetHelpers.h"

using mu_pp::ParamKind;
using mu_pp::writeKindedProperty;
using mu_pp::readKindedPropertyAsActualV2;
using mu_pp::kGlobalParamDefs;
using mu_pp::kGlobalParamDefCount;
using mu_pp::GlobalParamDef;

class GlobalParamDefsTest : public juce::UnitTest
{
public:
    GlobalParamDefsTest() : juce::UnitTest ("kGlobalParamDefs round-trip", "Preset") {}

    void runTest() override
    {
        beginTest ("Every kGlobalParamDefs entry round-trips a representative value");

        for (int i = 0; i < kGlobalParamDefCount; ++i)
        {
            const auto& def = kGlobalParamDefs[i];
            const juce::String id (def.id);

            // Pick a representative value per kind:
            //   Float          -> 0.5
            //   Int            -> 2
            //   Bool           -> 1.0 (true)
            //   AlgorithmIndex -> 1 (first non-None / non-zero entry)
            float testValue = 0.5f;
            switch (def.kind)
            {
                case ParamKind::Int:            testValue = 2.0f;  break;
                case ParamKind::Bool:           testValue = 1.0f;  break;
                case ParamKind::AlgorithmIndex: testValue = 1.0f;  break;
                default: break;
            }

            juce::ValueTree t ("G");
            writeKindedProperty (t, id, testValue, def.kind, def.algorithmNames);

            expect (t.hasProperty (id),
                    "writeKindedProperty should have set property '" + id + "'");

            const float back = readKindedPropertyAsActualV2 (t, id, def.kind, def.algorithmNames);
            expectWithinAbsoluteError (back, testValue, 1e-4f,
                "id '" + id + "' did not round-trip "
                + juce::String (testValue) + " (got " + juce::String (back) + ")");
        }
    }
};

static GlobalParamDefsTest globalParamDefsTest;
