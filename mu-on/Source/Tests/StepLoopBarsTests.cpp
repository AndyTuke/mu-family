// A step's loop: its Length in bars and the Bars it plays in. Length 6 with Bars 1, 2 and 5 ticked plays in those three passes of
// every six; the default (Length 1) plays every pass; an older file's N:M ratio loads as Length M with only bar N ticked.

#include "Sequencer/StepClock.h"
#include "Sequencer/StepRow.h"
#include <juce_core/juce_core.h>

namespace
{
    using mu_on::StepClock;
    using mu_on::StepRow;

    class StepLoopBarsTest : public juce::UnitTest
    {
    public:
        StepLoopBarsTest() : juce::UnitTest("mu-on step loop: Length and Bars", "Sequencer") {}

        void runTest() override
        {
            beginTest("a new step plays every pass");
            {
                StepRow row;
                expectEquals(row.stepLoopLength(0), 1);
                expectEquals(row.stepLoopBars(0), (int) StepRow::kAllBars);
                for (long long pass = -3; pass < 20; ++pass) expect(StepClock::loopPlays(row, 0, pass));
            }

            beginTest("Length 6 with Bars 1, 2 and 5 plays in those bars of every six, forwards and backwards");
            {
                StepRow row;
                row.setStepLoopLength(3, 6);
                row.setStepLoopBars(3, (1 << 0) | (1 << 1) | (1 << 4));
                for (long long pass = -12; pass < 24; ++pass)
                {
                    const int bar = (int) (((pass % 6) + 6) % 6);   // 0-based
                    expect(StepClock::loopPlays(row, 3, pass) == (bar == 0 || bar == 1 || bar == 4), "pass " + juce::String(pass));
                }
                expect(StepClock::loopPlays(row, 4, 2), "another step keeps its own default");
            }

            beginTest("with every bar ticked the step plays every pass whatever the Length; with none ticked it never plays");
            {
                StepRow row;
                row.setStepLoopLength(0, 8);
                for (long long pass = 0; pass < 24; ++pass) expect(StepClock::loopPlays(row, 0, pass));
                row.setStepLoopBars(0, 0);
                for (long long pass = 0; pass < 24; ++pass) expect(! StepClock::loopPlays(row, 0, pass));
            }

            beginTest("Length and Bars are held to their ranges (1..8 bars, bars 1..8 only)");
            {
                StepRow row;
                row.setStepLoopLength(0, 0);   expectEquals(row.stepLoopLength(0), 1);
                row.setStepLoopLength(0, 99);  expectEquals(row.stepLoopLength(0), 8);
                row.setStepLoopBars(0, 0x1FF); expectEquals(row.stepLoopBars(0), 0xFF);
            }

            beginTest("Length and Bars survive a save; a step left at the default writes nothing");
            {
                StepRow row;
                row.setStepLoopLength(2, 6);
                row.setStepLoopBars(2, (1 << 0) | (1 << 4));
                const auto saved = row.serialise(0);
                StepRow loaded;
                loaded.deserialise(saved);
                expectEquals(loaded.stepLoopLength(2), 6);
                expectEquals(loaded.stepLoopBars(2), (1 << 0) | (1 << 4));
                expectEquals(loaded.stepLoopLength(0), 1);

                StepRow plain;
                expect(! plain.serialise(0).hasProperty("loopLen"), "no loop properties for a row left at its defaults");
            }

            beginTest("an older file's N:M ratio loads as Length M with only bar N ticked");
            {
                // The old digit per step indexed {1:1, 1:2, 2:2, 1:3, 2:3, 3:3, 1:4, 2:4, 3:4, 4:4}.
                juce::ValueTree v("Track");
                v.setProperty("on", "1111000000000000", nullptr);
                v.setProperty("accent", "0000000000000000", nullptr);
                v.setProperty("loop", "0258000000000000", nullptr);   // 1:1, 2:2, 3:3, 3:4
                StepRow row;
                row.deserialise(v);
                expectEquals(row.stepLoopLength(0), 1);
                expectEquals(row.stepLoopLength(1), 2);  expectEquals(row.stepLoopBars(1), 1 << 1);
                expectEquals(row.stepLoopLength(2), 3);  expectEquals(row.stepLoopBars(2), 1 << 2);
                expectEquals(row.stepLoopLength(3), 4);  expectEquals(row.stepLoopBars(3), 1 << 2);
            }
        }
    };

    StepLoopBarsTest stepLoopBarsTest;
}
