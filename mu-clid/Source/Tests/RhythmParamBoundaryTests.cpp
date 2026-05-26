// Boundary and clamp tests for kRhythmParamDefs apply/push lambdas.
//
// Companion to RhythmParamRoundTripTests which proves every entry round-trips
// at value 1.0f. These tests cover:
//   - Min and max of clamped ranges actually hit their limits and round-trip
//   - Over-range inputs are clamped (never escape into undefined behaviour)
//   - Sentinel values with special semantics are preserved (aEnvRel >= 10.0,
//     rstSt = -1 → nullopt)
//   - AlgorithmIndex params clamp to countNames()-1, not a stale hardcoded
//     limit (regression test for #480: drvChar clamped to 12, not 13)
//   - Bool params serialise as 0.0 / 1.0 only
//   - patternDirty / voiceDirty are set by the correct param families

#include <juce_core/juce_core.h>
#include "../Persistence/RhythmParamTable.h"
#include "Audio/AlgorithmNames.h"

class RhythmParamBoundaryTest : public juce::UnitTest
{
public:
    RhythmParamBoundaryTest() : juce::UnitTest ("Rhythm param boundary/clamp", "Preset") {}

    void runTest() override
    {
        constexpr float kTol = 1e-4f;

        auto applyAndPush = [](const mu_pp::RhythmParamDef& def, float in) -> float {
            Rhythm r;
            bool pd = false, vd = false;
            def.apply(in, r, pd, vd);
            return def.push(r);
        };

        auto find = [](const char* suffix) -> const mu_pp::RhythmParamDef& {
            for (int i = 0; i < mu_pp::kRhythmParamCount; ++i)
                if (juce::String(mu_pp::kRhythmParamDefs[i].suffix) == suffix)
                    return mu_pp::kRhythmParamDefs[i];
            jassertfalse;
            return mu_pp::kRhythmParamDefs[0];
        };

        // ── HitGen integer clamps ─────────────────────────────────────────────
        beginTest ("stepsA: clamps to [1, 64], over-range clamped");
        {
            auto& def = find("stepsA");
            expectWithinAbsoluteError (applyAndPush(def, 1.0f),   1.0f,  kTol, "min");
            expectWithinAbsoluteError (applyAndPush(def, 64.0f), 64.0f,  kTol, "max");
            expectWithinAbsoluteError (applyAndPush(def, 0.0f),   1.0f,  kTol, "0 should clamp to 1");
            expectWithinAbsoluteError (applyAndPush(def, 100.0f), 64.0f, kTol, "100 should clamp to 64");
        }

        beginTest ("hitsA: clamps to [0, 64]");
        {
            auto& def = find("hitsA");
            expectWithinAbsoluteError (applyAndPush(def, 0.0f),    0.0f,  kTol, "min");
            expectWithinAbsoluteError (applyAndPush(def, 64.0f),  64.0f,  kTol, "max");
            expectWithinAbsoluteError (applyAndPush(def, -1.0f),   0.0f,  kTol, "-1 clamps to 0");
            expectWithinAbsoluteError (applyAndPush(def, 100.0f), 64.0f,  kTol, "100 clamps to 64");
        }

        beginTest ("prePadA: clamps to [0, 12]");
        {
            auto& def = find("prePadA");
            expectWithinAbsoluteError (applyAndPush(def, 0.0f),  0.0f,  kTol, "min");
            expectWithinAbsoluteError (applyAndPush(def, 12.0f), 12.0f, kTol, "max");
            expectWithinAbsoluteError (applyAndPush(def, 20.0f), 12.0f, kTol, "20 clamps to 12");
        }

        beginTest ("insStA: clamps to [0, 63]");
        {
            auto& def = find("insStA");
            expectWithinAbsoluteError (applyAndPush(def, 0.0f),  0.0f,  kTol, "min");
            expectWithinAbsoluteError (applyAndPush(def, 63.0f), 63.0f, kTol, "max");
            expectWithinAbsoluteError (applyAndPush(def, 99.0f), 63.0f, kTol, "99 clamps to 63");
        }

        beginTest ("insLenA: clamps to [0, 8]");
        {
            auto& def = find("insLenA");
            expectWithinAbsoluteError (applyAndPush(def, 0.0f), 0.0f, kTol, "min");
            expectWithinAbsoluteError (applyAndPush(def, 8.0f), 8.0f, kTol, "max");
            expectWithinAbsoluteError (applyAndPush(def, 50.0f), 8.0f, kTol, "50 clamps to 8");
        }

        // ── Pitch integer clamps ──────────────────────────────────────────────
        beginTest ("pitchOct: clamps to [-3, 3] (range reduced from ±4 per #640; ±4 oct combined max comes from oct + semi)");
        {
            auto& def = find("pitchOct");
            expectWithinAbsoluteError (applyAndPush(def, -3.0f),  -3.0f, kTol, "min");
            expectWithinAbsoluteError (applyAndPush(def, 3.0f),    3.0f, kTol, "max");
            expectWithinAbsoluteError (applyAndPush(def, -10.0f), -3.0f, kTol, "-10 clamps to -3");
            expectWithinAbsoluteError (applyAndPush(def, 10.0f),   3.0f, kTol, "10 clamps to 3");
        }

        beginTest ("pitchSemi: clamps to [-12, 12]");
        {
            auto& def = find("pitchSemi");
            expectWithinAbsoluteError (applyAndPush(def, -12.0f), -12.0f, kTol, "min");
            expectWithinAbsoluteError (applyAndPush(def, 12.0f),   12.0f, kTol, "max");
            expectWithinAbsoluteError (applyAndPush(def, -24.0f), -12.0f, kTol, "-24 clamps to -12");
        }

        // ── ADSR sustain 0-100 ↔ 0-1 scaling ─────────────────────────────────
        beginTest ("pEnvSus: display 0–100 round-trips via stored 0–1");
        {
            auto& def = find("pEnvSus");
            expectWithinAbsoluteError (applyAndPush(def, 0.0f),   0.0f,   kTol, "min");
            expectWithinAbsoluteError (applyAndPush(def, 100.0f), 100.0f, kTol, "max");
            expectWithinAbsoluteError (applyAndPush(def, 50.0f),  50.0f,  kTol, "midpoint");
        }

        beginTest ("aEnvSus: display 0–100 round-trips via stored 0–1");
        {
            auto& def = find("aEnvSus");
            expectWithinAbsoluteError (applyAndPush(def, 0.0f),   0.0f,   kTol, "min");
            expectWithinAbsoluteError (applyAndPush(def, 100.0f), 100.0f, kTol, "max");
            expectWithinAbsoluteError (applyAndPush(def, 80.0f),  80.0f,  kTol, "80%");
        }

        // ── aEnvRel sentinel (>= 10 = play to end) ──────────────────────────
        beginTest ("aEnvRel: sentinel 10.0 sets ampRelToEnd, round-trips as 10.0");
        {
            auto& def = find("aEnvRel");

            // Normal release
            expectWithinAbsoluteError (applyAndPush(def, 0.5f), 0.5f, kTol, "0.5s release");

            // Sentinel
            expectWithinAbsoluteError (applyAndPush(def, 10.0f), 10.0f, kTol, "sentinel 10.0 round-trips");

            Rhythm r;
            bool pd = false, vd = false;

            def.apply(10.0f, r, pd, vd);
            expect (r.voiceParams.ampRelToEnd, "ampRelToEnd should be true at sentinel 10.0");

            def.apply(9.9f, r, pd, vd);
            expect (!r.voiceParams.ampRelToEnd, "ampRelToEnd should be false at 9.9");
        }

        // ── rstSt sentinel (-1 = free-running) ───────────────────────────────
        beginTest ("rstSt: -1 maps to nullopt; positive clamped to [1, 256]");
        {
            auto& def = find("rstSt");

            // Free-running
            {
                Rhythm r;
                bool pd = false, vd = false;
                def.apply(-1.0f, r, pd, vd);
                expect (!r.resetSteps.has_value(), "rstSt=-1 should produce nullopt");
                expectWithinAbsoluteError (def.push(r), -1.0f, kTol, "nullopt pushes as -1");
            }

            // Fixed cycle of 16 steps
            {
                Rhythm r;
                bool pd = false, vd = false;
                def.apply(16.0f, r, pd, vd);
                expect (r.resetSteps.has_value() && r.resetSteps.value() == 16,
                        "rstSt=16 should store 16");
                expectWithinAbsoluteError (def.push(r), 16.0f, kTol, "16 pushes back correctly");
            }

            // Max boundary
            {
                Rhythm r;
                bool pd = false, vd = false;
                def.apply(256.0f, r, pd, vd);
                expectWithinAbsoluteError (def.push(r), 256.0f, kTol, "max boundary 256");
            }
        }

        // ── AlgorithmIndex: drvChar regression for #480 ───────────────────────
        beginTest ("drvChar: clamps to countNames()-1, not a stale hardcoded limit");
        {
            auto& def = find("drvChar");
            const int maxIdx = mu_audio::countNames(mu_audio::kInsertAlgorithmNames) - 1;

            expectWithinAbsoluteError (applyAndPush(def, 0.0f),           0.0f,           kTol, "None (0)");
            expectWithinAbsoluteError (applyAndPush(def, (float)maxIdx), (float)maxIdx,   kTol, "max valid index");
            expectWithinAbsoluteError (applyAndPush(def, (float)(maxIdx + 5)), (float)maxIdx, kTol,
                                       "over-range clamps to maxIdx, not to stale 12");
        }

        beginTest ("fltType: clamps to [0, 15]");
        {
            auto& def = find("fltType");
            expectWithinAbsoluteError (applyAndPush(def, 0.0f),  0.0f,  kTol, "min");
            expectWithinAbsoluteError (applyAndPush(def, 15.0f), 15.0f, kTol, "max");
            expectWithinAbsoluteError (applyAndPush(def, 99.0f), 15.0f, kTol, "99 clamps to 15");
        }

        // ── Bool params ───────────────────────────────────────────────────────
        beginTest ("All Bool params: 0.0 and 1.0 round-trip exactly");
        {
            for (int i = 0; i < mu_pp::kRhythmParamCount; ++i)
            {
                const auto& def = mu_pp::kRhythmParamDefs[i];
                if (def.kind != mu_pp::ParamKind::Bool) continue;
                const juce::String s (def.suffix);
                expectWithinAbsoluteError (applyAndPush(def, 0.0f), 0.0f, kTol, s + " Bool 0 round-trip");
                expectWithinAbsoluteError (applyAndPush(def, 1.0f), 1.0f, kTol, s + " Bool 1 round-trip");
            }
        }

        // ── patternDirty / voiceDirty flag families ───────────────────────────
        beginTest ("Pattern params set patternDirty; voice params set voiceDirty");
        {
            auto checkFlags = [&](const char* suffix, bool expectPd, bool expectVd,
                                  float val = 4.0f)
            {
                auto& def = find(suffix);
                Rhythm r;
                bool pd = false, vd = false;
                def.apply(val, r, pd, vd);
                juce::String s (suffix);
                if (expectPd) expect (pd, s + " should set patternDirty");
                else          expect (!pd, s + " should NOT set patternDirty");
                if (expectVd) expect (vd, s + " should set voiceDirty");
                else          expect (!vd, s + " should NOT set voiceDirty");
            };

            checkFlags("stepsA",    true,  false);
            checkFlags("hitsA",     true,  false);
            checkFlags("rotA",      true,  false);
            checkFlags("prePadA",   true,  false);
            checkFlags("logic",     true,  false, 1.0f);
            checkFlags("pitchOct",  false, true);
            checkFlags("pitchSemi", false, true);
            checkFlags("fltType",   false, true);
            checkFlags("fltCut",    false, true, 1000.0f);
            checkFlags("ampLvl",    false, true, 0.8f);
            checkFlags("accentDb",  false, true, 6.0f);
            checkFlags("drvChar",   false, true, 1.0f);
        }
    }
};

static RhythmParamBoundaryTest rhythmParamBoundaryTest;
