// A step's condition: Always plays it, Fill plays it only while the Fill button is held, Pre only if the previous step on the
// row played, NotPre only if it did not. The previous step is the nearest earlier on-step whose own condition is not Pre or
// NotPre, looking back across the row's wrap; it counts as having played when its loop, condition and probability all passed.

#include "Plugin/PluginProcessor.h"
#include "Tests/ProcessorSeamHelpers.h"

namespace
{
    using mu_on::StepRow;

    class StepConditionTest : public juce::UnitTest
    {
    public:
        StepConditionTest() : juce::UnitTest("mu-on step conditions: Fill, Pre and NotPre", "Sequencer") {}

        void runTest() override
        {
            mu_seam_test::TempStorage storage;
            mu_on::PluginProcessor p;
            auto& track = p.getTrack(0);
            auto& row = track.row;
            for (int s = 0; s < StepRow::kMaxSteps; ++s) { row.setOn(s, false); row.setStepCondition(s, StepRow::kCondAlways); }

            beginTest("Always plays, with or without Fill");
            {
                row.setOn(0, true);
                for (long long pass = 0; pass < 8; ++pass) { expect(track.stepPlays(0, pass, false)); expect(track.stepPlays(0, pass, true)); }
            }

            beginTest("Fill plays only while Fill is held");
            {
                row.setStepCondition(0, StepRow::kCondFill);
                for (long long pass = 0; pass < 8; ++pass) { expect(! track.stepPlays(0, pass, false)); expect(track.stepPlays(0, pass, true)); }
                row.setStepCondition(0, StepRow::kCondAlways);
            }

            beginTest("Pre plays if the previous step played, NotPre if it did not");
            {
                // Step 0 plays in bar 1 of a two-bar loop; step 4 is Pre, step 8 is NotPre.
                row.setStepLoopLength(0, 2);
                row.setStepLoopBars(0, 1);
                row.setOn(4, true);  row.setStepCondition(4, StepRow::kCondPre);
                row.setOn(8, true);  row.setStepCondition(8, StepRow::kCondNotPre);
                for (long long pass = 0; pass < 8; ++pass)
                {
                    const bool zeroPlays = track.stepPlays(0, pass, false);
                    expect(zeroPlays == (pass % 2 == 0), "step 0 plays in the odd bars of its loop, pass " + juce::String(pass));
                    expect(track.stepPlays(4, pass, false) == zeroPlays, "Pre follows step 0, pass " + juce::String(pass));
                    // Step 8's previous step is step 0 too (step 4 is a Pre, which never decides another Pre or NotPre).
                    expect(track.stepPlays(8, pass, false) == ! zeroPlays, "NotPre is the opposite, pass " + juce::String(pass));
                }
            }

            beginTest("a Pre looks back past other Pre steps to the last step with a plain condition");
            {
                row.setStepCondition(8, StepRow::kCondPre);   // steps 4 and 8 are both Pre now; 8 must follow step 0, not step 4
                for (long long pass = 0; pass < 6; ++pass)
                    expect(track.stepPlays(8, pass, false) == track.stepPlays(0, pass, false), "pass " + juce::String(pass));
                row.setStepCondition(8, StepRow::kCondNotPre);
            }

            beginTest("the row wraps: step 0's own Pre looks at the last plain step of the pass before");
            {
                for (int s = 0; s < StepRow::kMaxSteps; ++s) { row.setOn(s, false); row.setStepCondition(s, StepRow::kCondAlways); row.setStepLoopLength(s, 1); row.setStepLoopBars(s, StepRow::kAllBars); }
                row.setOn(0, true);  row.setStepCondition(0, StepRow::kCondPre);
                row.setOn(12, true);   // always plays
                for (long long pass = 0; pass < 4; ++pass) expect(track.stepPlays(0, pass, false), "Pre after step 12, pass " + juce::String(pass));
            }

            beginTest("with no earlier plain step Pre does not play and NotPre does");
            {
                for (int s = 0; s < StepRow::kMaxSteps; ++s) row.setOn(s, false);
                row.setOn(5, true);
                row.setStepCondition(5, StepRow::kCondPre);
                expect(! track.stepPlays(5, 3, false));
                row.setStepCondition(5, StepRow::kCondNotPre);
                expect(track.stepPlays(5, 3, false));
            }

            beginTest("a step's condition survives a save, and a row left at Always writes nothing");
            {
                StepRow r;
                r.setStepCondition(3, StepRow::kCondFill);
                r.setStepCondition(7, StepRow::kCondNotPre);
                StepRow loaded;
                loaded.deserialise(r.serialise(0));
                expectEquals(loaded.stepCondition(3), (int) StepRow::kCondFill);
                expectEquals(loaded.stepCondition(7), (int) StepRow::kCondNotPre);
                expectEquals(loaded.stepCondition(0), (int) StepRow::kCondAlways);
                StepRow plain;
                expect(! plain.serialise(0).hasProperty("cond"));
            }

            beginTest("the Fill button's state reaches the processor and is not left held");
            {
                expect(! p.isTrackFillHeld());
                p.setTrackFillHeld(true);
                expect(p.isTrackFillHeld());
                p.setTrackFillHeld(false);
                expect(! p.isTrackFillHeld());
            }
        }
    };

    StepConditionTest stepConditionTest;
}
