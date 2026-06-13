// apply/push lambda round-trip for every entry in kRhythmParamDefs.
//
// For each parameter, the test:
//   1. Picks a representative test value in display-scale
//   2. Calls def.apply(value, rhythm, pd, vd) to write into the Rhythm
//   3. Calls def.push(rhythm) to read it back
//   4. Asserts the round-tripped value equals the original (within precision)
//
// Catches the failure mode where the apply / push lambdas for one suffix get
// out of sync — e.g. push writes `vp.x` but apply reads to `vp.y`. That kind
// of typo would silently corrupt every preset save → load cycle.
//
// Why this test matters: before the table consolidation, applyRhythmSuffix's if/else chain and
// pushRhythmToAPVTS's set() chain were two independent hand-written
// implementations of the same suffix dispatch. They were vulnerable to drift.
// The table consolidation made them share code, but only this test
// proves the shared code is correct.

#include <juce_core/juce_core.h>
#include "../Persistence/RhythmParamTable.h"

class RhythmParamRoundTripTest : public juce::UnitTest
{
public:
    RhythmParamRoundTripTest() : juce::UnitTest ("Rhythm param apply/push round-trip", "Preset") {}

    void runTest() override
    {
        beginTest ("Every kRhythmParamDefs entry round-trips a representative value");

        for (int i = 0; i < mu_pp::kRhythmParamCount; ++i)
        {
            const auto& def = mu_pp::kRhythmParamDefs[i];
            const juce::String suffix (def.suffix);

            // Pick a test value that makes sense for the suffix. We use the
            // first value the parameter's clamp range will accept so we never
            // need to know the per-suffix range out-of-band. For bools we use
            // 1.0 (true). For algorithm-selectors we use index 1 (skips None=0
            // which is also the default — distinguishes "really wrote" from
            // "uninitialised"). For everything else we use 1.0.
            float testValue = 1.0f;
            if (def.kind == mu_pp::ParamKind::AlgorithmIndex)
                testValue = 1.0f;   // index 1 — first non-None entry for both insert and filter

            // Some suffixes have their own clamp ranges in apply() — e.g.
            // pitchOctave clamps to -3..3, so 1.0 fits. logic clamps 0..4,
            // so 1.0 fits. We pick values inside every existing clamp.

            Rhythm r;
            bool pd = false, vd = false;
            def.apply (testValue, r, pd, vd);

            const float roundTripped = def.push (r);

            // Bools and ints quantise — push returns the de-quantised value.
            // AlgorithmIndex stores an int internally → push returns float idx.
            // Use a tolerance generous enough for ADSR sustain (display-scale
            // 0..100 ↔ 0..1 storage means 1.0 input → 0.01 stored → 1.0 readback,
            // exact). Floats with no transformation should be exact too.
            const float tolerance = 1e-4f;
            expectWithinAbsoluteError (roundTripped, testValue, tolerance,
                "suffix '" + suffix + "' did not round-trip "
                + juce::String (testValue) + " (got " + juce::String (roundTripped) + ")");
        }
    }
};

static RhythmParamRoundTripTest rhythmParamRoundTripTest;
