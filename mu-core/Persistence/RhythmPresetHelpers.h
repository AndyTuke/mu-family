#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include "Persistence/RhythmParamTable.h"   // ParamKind, RhythmParamDef
#include "Audio/AlgorithmNames.h" // nameFromIndex, indexFromName, kEffect*Names, kInsert*Names

// Kinded-property reader / writer for the per-rhythm parameter table (RhythmParamTable.h): the v2
// preset encoding of each ParamKind. Header-only so tests can #include without pulling in a
// processor.

namespace mu_core {

// Write a float `actualValue` into `tree` under `propName` in the v2 format
// appropriate for ParamKind:
//   Bool           → "true" / "false" string
//   Int            → integer XML property
//   AlgorithmIndex → stable name string from algorithmNames; integer fallback
//   Float          → raw float
inline void writeKindedProperty(juce::ValueTree& tree,
                                const juce::String& propName,
                                float actualValue,
                                ParamKind kind,
                                const char* const* algorithmNames)
{
    switch (kind)
    {
        case ParamKind::Bool:
            tree.setProperty(propName, actualValue >= 0.5f ? "true" : "false", nullptr);
            break;
        case ParamKind::Int:
            tree.setProperty(propName, juce::roundToInt(actualValue), nullptr);
            break;
        case ParamKind::AlgorithmIndex:
            if (algorithmNames != nullptr)
            {
                const int idx = juce::roundToInt(actualValue);
                if (const char* name = mu_audio::nameFromIndex(algorithmNames, idx))
                {
                    tree.setProperty(propName, juce::String(name), nullptr);
                    break;
                }
            }
            // Unknown name — fall back to writing the integer index.
            tree.setProperty(propName, juce::roundToInt(actualValue), nullptr);
            break;
        case ParamKind::Float:
        default:
            tree.setProperty(propName, actualValue, nullptr);
            break;
    }
}

// Read `propName` from `tree` and return as an actual de-normalised float using
// the v2 encoding rules for `kind`. Caller must check hasProperty before calling
// if an absent-property sentinel is needed.
inline float readKindedPropertyAsActualV2(const juce::ValueTree& tree,
                                          const juce::String& propName,
                                          ParamKind kind,
                                          const char* const* algorithmNames)
{
    switch (kind)
    {
        case ParamKind::Bool:
            return (bool) tree.getProperty(propName) ? 1.0f : 0.0f;
        case ParamKind::Int:
            return (float) (int) tree.getProperty(propName);
        case ParamKind::AlgorithmIndex:
            if (algorithmNames != nullptr)
            {
                const juce::String name = tree.getProperty(propName).toString();
                const int idx = mu_audio::indexFromName(algorithmNames, name);
                if (idx >= 0)
                    return (float) idx;
                // Unknown name — fall through to numeric parse (legacy fallback writes).
                return (float) (int) tree.getProperty(propName);
            }
            return (float) (int) tree.getProperty(propName);
        case ParamKind::Float:
        default:
            return (float) (double) tree.getProperty(propName);
    }
}

} // namespace mu_core
