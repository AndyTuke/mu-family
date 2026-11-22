// Copying and moving a whole step (the drag-to-move / Ctrl-drag-copy of the step grid): every setting comes along, the copy is
// not tied to its neighbour, a move clears the source.

#include "Sequencer/StepRow.h"
#include <juce_core/juce_core.h>

namespace
{
    using mu_on::StepRow;

    class StepCopyTest : public juce::UnitTest
    {
    public:
        StepCopyTest() : juce::UnitTest("mu-on step copy and move", "Sequencer") {}

        void runTest() override
        {
            beginTest("copyStep carries the step's settings and leaves the source alone");
            {
                StepRow row;
                row.setOn(2, true);
                row.setAccent(2, true);
                row.setStepProb(2, 40);
                row.setStepLoopLength(2, 4);
                row.setStepSemitones(2, 5);
                row.copyStep(2, 9);
                expect(row.isOn(9) && row.isAccent(9));
                expectEquals(row.stepProb(9), 40);
                expectEquals(row.stepLoopLength(9), 4);
                expectEquals(row.stepSemitones(9), 5);
                expect(row.isOn(2) && row.isAccent(2), "the source is untouched");
            }

            beginTest("the copy replaces what was on the target, and is not tied");
            {
                StepRow row;
                row.setOn(1, true);
                row.setOn(2, true);
                row.setTied(2, true);
                row.setOn(6, true);
                row.setStepProb(6, 10);
                row.setOn(5, true);
                row.copyStep(2, 6);
                expectEquals(row.stepProb(6), 100, "the target's old probability is gone");
                expect(! row.isTied(6), "a copy is not tied to its neighbour");
            }

            beginTest("resetStep (the source of a move) returns the step to defaults and ends the tie into the next one");
            {
                StepRow row;
                row.setOn(3, true);
                row.setAccent(3, true);
                row.setOn(4, true);
                row.setTied(4, true);
                row.copyStep(3, 8);
                row.resetStep(3);
                expect(! row.isOn(3) && ! row.isAccent(3));
                expect(! row.isTied(4), "the step after no longer continues a note that moved away");
                expect(row.isOn(8) && row.isAccent(8), "the moved step arrived");
            }
        }
    };

    static StepCopyTest stepCopyTest;
}
