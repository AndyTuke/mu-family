// Preset XML codec round-trip tests.
//
// Proves that writeKindedProperty + readKindedPropertyAsActualV2 form a lossless
// codec for every kRhythmParamDefs entry when a Rhythm is serialised and
// deserialised via a ValueTree (the .muRhythm / .muClid XML format).
//
// Test path for each parameter:
//   1. apply(testValue, rhythm)        - write a non-default value into a Rhythm
//   2. push(rhythm)                    - read it back as an actual float
//   3. writeKindedProperty(tree, ...)  - serialise into a ValueTree (XML format)
//   4. readKindedPropertyAsActualV2    - deserialise back to float
//   5. apply(readBack, rhythm2)        - write into a second Rhythm
//   6. push(rhythm2) ≈ push(rhythm)    - verify the values survived the codec
//
// This catches the failure mode where the XML codec mis-encodes a value -
// e.g. a Bool written as int instead of "true"/"false" string, or an
// AlgorithmIndex written as an integer index when the v2 reader expects a
// name string. That class of bug silently corrupted preset save/load in the
// v0/v1 formats.
//
// Companion tests: RhythmParamRoundTripTests proves apply/push are inverses;
// KindedPropertyRoundTripTests proves the codec primitives individually.
// This test proves the full stack - Rhythm -> XML -> Rhythm - end-to-end.

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include "../Persistence/RhythmParamTable.h"
#include "../Persistence/PresetHelpers.h"

using mu_pp::ParamKind;
using mu_pp::writeKindedProperty;
using mu_pp::readKindedPropertyAsActualV2;

class PresetXMLRoundTripTest : public juce::UnitTest
{
public:
    PresetXMLRoundTripTest() : juce::UnitTest ("Preset XML round-trip", "Preset") {}

    void runTest() override
    {
        constexpr float kTol = 1e-4f;
        constexpr const char* kPrefix = "r0_";

        // ── Full Rhythm XML codec round-trip ──────────────────────────────────
        // For every kRhythmParamDefs entry: apply a test value, serialise to
        // ValueTree, deserialise, and verify the round-tripped push() matches.
        beginTest ("All kRhythmParamDefs entries survive a ValueTree encode/decode cycle");
        {
            juce::ValueTree state ("RhythmPreset");

            // Pass 1: populate a Rhythm with test values and serialise.
            Rhythm r1;
            for (int i = 0; i < mu_pp::kRhythmParamCount; ++i)
            {
                const auto& def = mu_pp::kRhythmParamDefs[i];
                bool pd = false, vd = false;

                // Choose a test value that every apply() will accept without clamping
                // to the initial default.  Use index 1 for algorithm selectors so we
                // distinguish "really wrote" from "uninitialised" (index 0 is default).
                float testVal = 1.0f;
                if (def.kind == ParamKind::AlgorithmIndex)
                    testVal = 1.0f;

                def.apply(testVal, r1, pd, vd);

                const juce::String propName = juce::String(kPrefix) + def.suffix;
                writeKindedProperty(state, propName, def.push(r1), def.kind, def.algorithmNames);
            }

            // Pass 2: deserialise into a fresh Rhythm.
            Rhythm r2;
            for (int i = 0; i < mu_pp::kRhythmParamCount; ++i)
            {
                const auto& def = mu_pp::kRhythmParamDefs[i];
                const juce::String propName = juce::String(kPrefix) + def.suffix;

                const float readBack = readKindedPropertyAsActualV2(
                    state, propName, def.kind, def.algorithmNames);

                bool pd = false, vd = false;
                def.apply(readBack, r2, pd, vd);
            }

            // Pass 3: compare push() values - they must match.
            for (int i = 0; i < mu_pp::kRhythmParamCount; ++i)
            {
                const auto& def = mu_pp::kRhythmParamDefs[i];
                const float original  = def.push(r1);
                const float recovered = def.push(r2);
                expectWithinAbsoluteError(recovered, original, kTol,
                    juce::String(def.suffix) + " XML round-trip: "
                    + juce::String(original, 4) + " -> stored -> "
                    + juce::String(recovered, 4));
            }
        }

        // ── Bool params: "true"/"false" encoding preserved ────────────────────
        beginTest ("Bool params: 0.0 and 1.0 survive as distinct XML values");
        {
            for (int i = 0; i < mu_pp::kRhythmParamCount; ++i)
            {
                const auto& def = mu_pp::kRhythmParamDefs[i];
                if (def.kind != ParamKind::Bool) continue;
                const juce::String s (def.suffix);

                for (float boolVal : { 0.0f, 1.0f })
                {
                    juce::ValueTree t ("T");
                    writeKindedProperty(t, "v", boolVal, ParamKind::Bool, nullptr);

                    // Verify the property is a string "true"/"false", not an int.
                    const auto prop = t.getProperty("v");
                    expect (prop.isString(),
                            s + " Bool should be written as string, not int/float");

                    const float back = readKindedPropertyAsActualV2(t, "v", ParamKind::Bool, nullptr);
                    expectWithinAbsoluteError(back, boolVal, kTol,
                        s + " Bool " + juce::String(boolVal) + " round-trip");
                }
            }
        }

        // ── AlgorithmIndex params: name-string encoding preserved ─────────────
        beginTest ("AlgorithmIndex params: written as name strings, read back correctly");
        {
            for (int i = 0; i < mu_pp::kRhythmParamCount; ++i)
            {
                const auto& def = mu_pp::kRhythmParamDefs[i];
                if (def.kind != ParamKind::AlgorithmIndex) continue;
                if (def.algorithmNames == nullptr) continue;

                const juce::String s (def.suffix);

                // Write index 1 - must produce a string property (not int).
                juce::ValueTree t ("T");
                writeKindedProperty(t, "v", 1.0f, ParamKind::AlgorithmIndex, def.algorithmNames);

                const auto prop = t.getProperty("v");
                expect (prop.isString(),
                        s + " AlgorithmIndex should be written as name string");

                const float back = readKindedPropertyAsActualV2(
                    t, "v", ParamKind::AlgorithmIndex, def.algorithmNames);
                expectWithinAbsoluteError(back, 1.0f, kTol,
                    s + " AlgorithmIndex 1 round-trip via name string");
            }
        }

        // ── rstSt sentinel: -1 (free-running) survives XML codec ─────────────
        beginTest ("rstSt: sentinel -1 (free-running) survives XML round-trip");
        {
            const mu_pp::RhythmParamDef* rstStDef = nullptr;
            for (int i = 0; i < mu_pp::kRhythmParamCount; ++i)
                if (juce::String(mu_pp::kRhythmParamDefs[i].suffix) == "rstSt")
                    rstStDef = &mu_pp::kRhythmParamDefs[i];

            if (rstStDef != nullptr)
            {
                // Sentinel -1 -> nullopt
                juce::ValueTree t ("T");
                writeKindedProperty(t, "v", -1.0f, rstStDef->kind, rstStDef->algorithmNames);
                const float back = readKindedPropertyAsActualV2(
                    t, "v", rstStDef->kind, rstStDef->algorithmNames);
                expectWithinAbsoluteError(back, -1.0f, kTol, "rstSt sentinel -1 XML round-trip");

                // Fixed cycle of 32 steps
                writeKindedProperty(t, "v", 32.0f, rstStDef->kind, rstStDef->algorithmNames);
                const float back32 = readKindedPropertyAsActualV2(
                    t, "v", rstStDef->kind, rstStDef->algorithmNames);
                expectWithinAbsoluteError(back32, 32.0f, kTol, "rstSt = 32 steps XML round-trip");
            }
        }

        // ── aEnvRel sentinel: 10.0 (play-to-end) survives XML codec ──────────
        beginTest ("aEnvRel: sentinel 10.0 (play-to-end) survives XML round-trip");
        {
            const mu_pp::RhythmParamDef* relDef = nullptr;
            for (int i = 0; i < mu_pp::kRhythmParamCount; ++i)
                if (juce::String(mu_pp::kRhythmParamDefs[i].suffix) == "aEnvRel")
                    relDef = &mu_pp::kRhythmParamDefs[i];

            if (relDef != nullptr)
            {
                juce::ValueTree t ("T");
                writeKindedProperty(t, "v", 10.0f, relDef->kind, relDef->algorithmNames);
                const float back = readKindedPropertyAsActualV2(
                    t, "v", relDef->kind, relDef->algorithmNames);
                expectWithinAbsoluteError(back, 10.0f, kTol, "aEnvRel sentinel 10.0 XML round-trip");

                // Normal release value (not sentinel)
                writeKindedProperty(t, "v", 0.5f, relDef->kind, relDef->algorithmNames);
                const float back05 = readKindedPropertyAsActualV2(
                    t, "v", relDef->kind, relDef->algorithmNames);
                expectWithinAbsoluteError(back05, 0.5f, kTol, "aEnvRel 0.5s XML round-trip");
            }
        }
    }
};

static PresetXMLRoundTripTest presetXMLRoundTripTest;
