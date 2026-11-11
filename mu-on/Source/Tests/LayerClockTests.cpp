// The layer's own clock: Speed (how long a step lasts), Layer Length against the sequence's length, Play Mode (forwards, backwards,
// random) and the sequence's Reset. The arithmetic is tested through StepClock.

#include "Plugin/PluginProcessor.h"
#include "Tests/ProcessorSeamHelpers.h"
#include "Sequencer/StepClock.h"

#include <set>
#include <vector>

namespace
{
    using mu_on::ClockSetup;
    using mu_on::StepClock;
    using mu_on::StepRow;

    class LayerClockTest : public juce::UnitTest
    {
    public:
        LayerClockTest() : juce::UnitTest("mu-on layer clock: speed, length, play mode, reset", "Sequencer") {}

        void runTest() override
        {
            beginTest("the default setup is the plain 909 clock: sixteenths, no reset, forwards");
            {
                ClockSetup c;
                expectEquals(c.stepBeats, 0.25);
                expectEquals(c.resetBeats, 0.0);
                expectEquals(c.mode, (int) ClockSetup::kForwards);
                expectEquals(StepClock::positionAt(1.0, 16, c).step, 4);
                expectEquals(StepClock::positionAt(4.0, 16, c).step, 0);   // wraps after a bar
            }

            beginTest("Speed: a step lasts as long as the setup says");
            {
                ClockSetup c;
                c.stepBeats = 0.5;   // eighths
                expectEquals(StepClock::positionAt(0.5, 16, c).step, 1);
                expectEquals(StepClock::positionAt(8.0, 16, c).step, 0);   // 16 eighths = 8 beats
                StepRow row;
                for (int s = 0; s < 16; ++s) row.setOn(s, true);
                StepClock clock;
                clock.setup(c);
                int fired = 0;
                clock.processSpan(row, 0.0, 4 * 4410, 1.0 / 4410.0, 0.0f, 0.0f, [&](float, int) { ++fired; });
                expectEquals(fired, 8, "four beats of eighths");
            }

            beginTest("Layer Length: a 12-step layer wraps after 12 steps and drifts against a 16-step one");
            {
                const ClockSetup c;
                expectEquals(StepClock::positionAt(3.0, 12, c).slot, 0);      // 12 sixteenths = 3 beats
                expectEquals(StepClock::positionAt(4.0, 12, c).slot, 4);      // the 16-step layer has just wrapped, this one is four in
                expectEquals(StepClock::positionAt(12.0, 12, c).slot, 0);     // they meet again after three bars (48 steps)
                expectEquals(StepClock::positionAt(12.0, 16, c).slot, 0);
            }

            beginTest("the row is held to the sequence's length but keeps its own");
            {
                StepRow row;
                row.setNumSteps(48);
                expectEquals(row.numSteps(), 48);
                row.setStepCap(32);
                expectEquals(row.numSteps(), 32);
                expectEquals(row.ownNumSteps(), 48);
                row.setStepCap(StepRow::kMaxSteps);
                expectEquals(row.numSteps(), 48, "a longer sequence brings the layer's length back");
            }

            beginTest("Play Mode: backwards counts down, random stays in range and repeats itself");
            {
                ClockSetup c;
                c.mode = ClockSetup::kBackwards;
                expectEquals(StepClock::positionAt(0.0, 16, c).step, 15);
                expectEquals(StepClock::positionAt(0.25, 16, c).step, 14);
                expectEquals(StepClock::positionAt(3.75, 16, c).step, 0);
                c.mode = ClockSetup::kRandom;
                c.seed = 1234u;
                std::vector<int> seen;
                for (int i = 0; i < 64; ++i)
                {
                    const int a = StepClock::positionAt(i * 0.25, 12, c).step;
                    const int b = StepClock::positionAt(i * 0.25, 12, c).step;
                    expectEquals(a, b, "the same time always picks the same step");
                    expect(a >= 0 && a < 12, "inside the layer's length");
                    seen.push_back(a);
                }
                expect(std::set<int>(seen.begin(), seen.end()).size() > 4, "and it does move around");
            }

            beginTest("Play Mode backwards plays the row from its last step");
            {
                StepRow row;
                row.setOn(15, true);
                ClockSetup c;
                c.mode = ClockSetup::kBackwards;
                StepClock clock;
                clock.setup(c);
                int firstOffset = -1, count = 0;
                clock.processSpan(row, 0.0, 4410, 1.0 / 4410.0, 0.0f, 0.0f, [&](float, int off) { if (count++ == 0) firstOffset = off; });
                expectEquals(count, 1, "step 15 plays first, in the first slot");
                expectEquals(firstOffset, 0);
            }

            beginTest("Reset: every layer starts over together at the reset bar");
            {
                ClockSetup c;
                c.resetBeats = 8.0;   // two bars
                expectEquals(StepClock::positionAt(7.75, 12, c).slot, 31 % 12);
                expectEquals(StepClock::positionAt(8.0, 12, c).slot, 0);       // back at its first step; unreset it would be 32 % 12 = 8
                expectEquals(StepClock::positionAt(8.25, 12, c).slot, 1);
                expectEquals(StepClock::positionAt(8.0, 12, ClockSetup {}).slot, 8);
                expect(StepClock::positionAt(8.0, 12, c).pass > StepClock::positionAt(7.75, 12, c).pass, "the pass keeps counting across a reset");

                StepRow row;
                for (int s = 0; s < 12; ++s) row.setOn(s, s == 0);
                row.setNumSteps(12);
                StepClock clock;
                clock.setup(c);
                std::vector<double> at;
                // Step 0 of the 12-step layer plays at beat 0, 3, 6 and again at 8 (the reset), then 11.
                clock.processSpan(row, 0.0, 8 * 100, 1.0 / 100.0, 0.0f, 0.0f, [&](float, int off) { at.push_back(off / 100.0); });
                clock.processSpan(row, 8.0, 4 * 100, 1.0 / 100.0, 0.0f, 0.0f, [&](float, int off) { at.push_back(8.0 + off / 100.0); });
                expectEquals((int) at.size(), 5);
                if (at.size() == 5)
                {
                    expectWithinAbsoluteError(at[0], 0.0, 1e-9);
                    expectWithinAbsoluteError(at[1], 3.0, 1e-9);
                    expectWithinAbsoluteError(at[2], 6.0, 1e-9);
                    expectWithinAbsoluteError(at[3], 8.0, 1e-9);
                    expectWithinAbsoluteError(at[4], 11.0, 1e-9);
                }
            }
        }
    };

    LayerClockTest layerClockTest;
}
