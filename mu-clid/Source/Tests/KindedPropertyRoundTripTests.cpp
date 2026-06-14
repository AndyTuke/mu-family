// writeKindedProperty / readKindedPropertyAsActualV2 round-trip test.
//
// For each ParamKind, picks a representative value, writes it into a ValueTree
// via writeKindedProperty, reads it back via readKindedPropertyAsActualV2, and
// asserts the values match. Catches format drift where the writer and reader
// disagree on how a kind is encoded - e.g. Bool writer emits "true" but reader
// expected an int 1, or AlgorithmIndex writer emits a name string but reader
// misparses it.

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include "../Persistence/PresetHelpers.h"

using mu_pp::ParamKind;
using mu_pp::writeKindedProperty;
using mu_pp::readKindedPropertyAsActualV2;

class KindedPropertyRoundTripTest : public juce::UnitTest
{
public:
    KindedPropertyRoundTripTest() : juce::UnitTest ("writeKindedProperty / readKindedPropertyAsActualV2 round-trip", "Preset") {}

    void runTest() override
    {
        beginTest ("Float round-trip");
        {
            juce::ValueTree t ("T");
            writeKindedProperty (t, "v", 123.456f, ParamKind::Float, nullptr);
            const float back = readKindedPropertyAsActualV2 (t, "v", ParamKind::Float, nullptr);
            expectWithinAbsoluteError (back, 123.456f, 1e-4f, "Float round-trip");
        }

        beginTest ("Int round-trip");
        {
            juce::ValueTree t ("T");
            writeKindedProperty (t, "v", 7.0f, ParamKind::Int, nullptr);
            const float back = readKindedPropertyAsActualV2 (t, "v", ParamKind::Int, nullptr);
            expectEquals ((int) back, 7, "Int round-trip");
        }

        beginTest ("Bool true round-trip");
        {
            juce::ValueTree t ("T");
            writeKindedProperty (t, "v", 1.0f, ParamKind::Bool, nullptr);
            // Bool is written as the string "true" - check that the property is
            // readable as a boolean and that the round-trip gives back 1.0.
            expect (t.getProperty ("v").toString() == "true", "Bool true should be written as string 'true'");
            const float back = readKindedPropertyAsActualV2 (t, "v", ParamKind::Bool, nullptr);
            expectWithinAbsoluteError (back, 1.0f, 1e-4f, "Bool true round-trip");
        }

        beginTest ("Bool false round-trip");
        {
            juce::ValueTree t ("T");
            writeKindedProperty (t, "v", 0.0f, ParamKind::Bool, nullptr);
            expect (t.getProperty ("v").toString() == "false", "Bool false should be written as string 'false'");
            const float back = readKindedPropertyAsActualV2 (t, "v", ParamKind::Bool, nullptr);
            expectWithinAbsoluteError (back, 0.0f, 1e-4f, "Bool false round-trip");
        }

        beginTest ("AlgorithmIndex round-trip via name string");
        {
            // Use kInsertAlgorithmNames: index 4 = "Bitcrusher"
            const char* const* names = mu_audio::kInsertAlgorithmNames;
            juce::ValueTree t ("T");
            writeKindedProperty (t, "v", 4.0f, ParamKind::AlgorithmIndex, names);
            // Should be persisted as "Bitcrusher", not the integer 4
            expect (t.getProperty ("v").toString() == "Bitcrusher",
                    "AlgorithmIndex should be written as name string");
            const float back = readKindedPropertyAsActualV2 (t, "v", ParamKind::AlgorithmIndex, names);
            expectWithinAbsoluteError (back, 4.0f, 1e-4f, "AlgorithmIndex round-trip via name");
        }

        beginTest ("AlgorithmIndex fallback: integer property still reads correctly");
        {
            // Simulate a legacy preset that wrote the integer index directly.
            const char* const* names = mu_audio::kInsertAlgorithmNames;
            juce::ValueTree t ("T");
            t.setProperty ("v", 4, nullptr);   // write raw int (legacy fallback)
            const float back = readKindedPropertyAsActualV2 (t, "v", ParamKind::AlgorithmIndex, names);
            expectWithinAbsoluteError (back, 4.0f, 1e-4f, "AlgorithmIndex integer fallback");
        }

        beginTest ("AlgorithmIndex with nullptr table falls back to integer");
        {
            juce::ValueTree t ("T");
            writeKindedProperty (t, "v", 3.0f, ParamKind::AlgorithmIndex, nullptr);
            // With no name table, writes an int
            const float back = readKindedPropertyAsActualV2 (t, "v", ParamKind::AlgorithmIndex, nullptr);
            expectWithinAbsoluteError (back, 3.0f, 1e-4f, "AlgorithmIndex null-table fallback");
        }
    }
};

static KindedPropertyRoundTripTest kindedPropertyRoundTripTest;
