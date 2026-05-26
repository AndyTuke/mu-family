#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include "RhythmParamTable.h"     // ParamKind, RhythmParamDef
#include "Audio/AlgorithmNames.h" // nameFromIndex, indexFromName, kEffect*Names, kInsert*Names

// Preset serialisation primitives shared between PluginProcessor_Preset.cpp
// and the test suite. Header-only so tests can #include without pulling in the
// full PluginProcessor include chain.

namespace mu_pp {

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

// per-global-param kind tags + algorithm-name pointers, mirroring kRhythmParamDefs.
// Global params don't have apply/push lambdas (they flow directly through APVTS
// rather than via a Rhythm struct).
struct GlobalParamDef
{
    const char*        id;
    ParamKind          kind            = ParamKind::Float;
    const char* const* algorithmNames  = nullptr;
};

inline const GlobalParamDef kGlobalParamDefs[] = {
    // ── Effect slot ──────────────────────────────────────────────────────
    { "eff_algo",      ParamKind::AlgorithmIndex, mu_audio::kEffectAlgorithmNames },
    { "eff_en",        ParamKind::Bool   },
    { "eff_send" },     { "eff_p0" }, { "eff_p1" }, { "eff_p2" }, { "eff_p3" }, { "eff_p4" },
    // ── Delay slot ───────────────────────────────────────────────────────
    { "dly_en",        ParamKind::Bool   },
    { "dly_mode",      ParamKind::Bool   },
    { "dly_ms" },
    { "dly_syncDenom", ParamKind::Int    },
    { "dly_syncDot",   ParamKind::Bool   },
    { "dly_syncTrip",  ParamKind::Bool   },
    { "dly_count",     ParamKind::Int    },
    { "dly_fb" }, { "dly_spread" }, { "dly_dirt" }, { "dly_send" },
    // ── Reverb slot ──────────────────────────────────────────────────────
    { "rev_algo",      ParamKind::AlgorithmIndex, mu_audio::kReverbAlgorithmNames },
    { "rev_en",        ParamKind::Bool   },
    { "rev_lvl" }, { "rev_size" }, { "rev_pre" }, { "rev_diff" },
    { "rev_damp" }, { "rev_mod" }, { "rev_dirt" },
    // ── Intra-FX routing ────────────────────────────────────────────────
    { "eff2dly" }, { "eff2rev" }, { "dly2rev" },
    // ── Echo ─────────────────────────────────────────────────────────────
    { "echo_en",       ParamKind::Bool   },
    { "echo_mode" },
    { "echo_ms" },
    { "echo_syncDenom",ParamKind::Int    },
    { "echo_syncDot",  ParamKind::Bool   },
    { "echo_syncTrip", ParamKind::Bool   },
    { "echo_count",    ParamKind::Int    },
    { "echo_fb" }, { "echo_spread" }, { "echo_dirt" },
    // ── Effect return ───────────────────────────────────────────────────
    { "ret_eff_lvl" }, { "ret_eff_pan" },
    { "ret_eff_mute",  ParamKind::Bool   },
    { "ret_eff_solo",  ParamKind::Bool   },
    { "ret_eff_scSrc", ParamKind::Int    },
    { "ret_eff_scAmt" }, { "ret_eff_scAtk" }, { "ret_eff_scRel" },
    // ── Delay return ────────────────────────────────────────────────────
    { "ret_dly_lvl" }, { "ret_dly_pan" },
    { "ret_dly_mute",  ParamKind::Bool   },
    { "ret_dly_solo",  ParamKind::Bool   },
    { "ret_dly_scSrc", ParamKind::Int    },
    { "ret_dly_scAmt" }, { "ret_dly_scAtk" }, { "ret_dly_scRel" },
    // ── Reverb return ───────────────────────────────────────────────────
    { "ret_rev_lvl" }, { "ret_rev_pan" },
    { "ret_rev_mute",  ParamKind::Bool   },
    { "ret_rev_solo",  ParamKind::Bool   },
    { "ret_rev_scSrc", ParamKind::Int    },
    { "ret_rev_scAmt" }, { "ret_rev_scAtk" }, { "ret_rev_scRel" },
    // ── Master ──────────────────────────────────────────────────────────
    { "mstr_lvl" }, { "mstr_pan" },
    { "mstrLoop",      ParamKind::Int    },
    // ── Master insert 1 ─────────────────────────────────────────────────
    // Stage 36: 4 generic Param slots (normalised 0..1) replace the prior 9
    // named master-insert fields. Single algo selector + algo-aware UI.
    { "mst_insChar",   ParamKind::AlgorithmIndex, mu_audio::kInsertAlgorithmNames },
    { "mst_insP1" }, { "mst_insP2" }, { "mst_insP3" }, { "mst_insP4" },
    // ── Master insert 2 ─────────────────────────────────────────────────
    { "mst_ins2Char",  ParamKind::AlgorithmIndex, mu_audio::kInsertAlgorithmNames },
    { "mst_ins2P1" }, { "mst_ins2P2" }, { "mst_ins2P3" }, { "mst_ins2P4" },
};

inline constexpr int kGlobalParamDefCount = (int)(sizeof(kGlobalParamDefs) / sizeof(kGlobalParamDefs[0]));

} // namespace mu_pp
