// The step row facts the paged Steps grid rests on. StepPager, TrackContent and TrackStepRow are mu-core UI and are not linked
// into this console target, so what is checked here is the model they read: how many pages a Length needs (the pager's page
// count and the page clamp), where a Length can end up after a shorter preset or a Length edit (the selected-step clamp is
// "no further than numSteps() - 1"), and the tie flag at the 15 -> 16 page edge (the tie stub on the first cell of a page
// is drawn only when tiedFrom() says the tie is real). The drawing, the buttons and the clamp call sites are in the smoke plan.

#include "Sequencer/StepRow.h"
#include <juce_core/juce_core.h>

namespace
{
    using mu_on::StepRow;

    class StepPagingTest : public juce::UnitTest
    {
    public:
        StepPagingTest() : juce::UnitTest("mu-on step row paging, Length and page-edge ties", "Sequencer") {}

        void runTest() override
        {
            beginTest("Page count follows the Length in whole pages of 16");
            {
                StepRow row;
                expectEquals(row.numSteps(), 16);   // a new row is one page
                expectEquals(row.numPages(), 1);
                // Each pair is a Length and the pages it needs: a partial last page still counts.
                const int cases[][2] = { { 1, 1 }, { 15, 1 }, { 16, 1 }, { 17, 2 }, { 32, 2 }, { 33, 3 }, { 48, 3 }, { 49, 4 }, { 64, 4 } };
                for (auto& c : cases)
                {
                    row.setNumSteps(c[0]);
                    expectEquals(row.numPages(), c[1], "Length " + juce::String(c[0]));
                }
            }

            beginTest("Length is held to 1..64, so there is always one page and never a fifth");
            {
                StepRow row;
                row.setNumSteps(0);    expectEquals(row.numSteps(), 1);  expectEquals(row.numPages(), 1);
                row.setNumSteps(-5);   expectEquals(row.numSteps(), 1);
                row.setNumSteps(65);   expectEquals(row.numSteps(), StepRow::kMaxSteps);  expectEquals(row.numPages(), 4);
                row.setNumSteps(1000); expectEquals(row.numSteps(), StepRow::kMaxSteps);
            }

            beginTest("Shrinking the Length takes pages away but keeps the hidden steps for when it grows again");
            {
                StepRow row;
                row.setNumSteps(64);
                row.setOn(40, true);
                row.setAccent(40, true);
                row.setNumSteps(16);                         // the owner's page 3 is gone; the last valid step is 15
                expectEquals(row.numPages(), 1);
                expectEquals(row.numSteps() - 1, 15);
                expect(row.isOn(40) && row.isAccent(40), "a step past the Length keeps its settings");
                row.setNumSteps(64);
                expect(row.isOn(40) && row.isAccent(40), "and shows them again when the row grows back");
            }

            beginTest("A preset swap to a shorter row leaves the last step at its Length - 1 (the selection clamp target)");
            {
                StepRow longRow;
                longRow.setNumSteps(64);
                longRow.setOn(60, true);
                const int selected = 60;                     // what a Step panel could be showing
                StepRow shortRow;                            // a 16-step preset
                shortRow.setOn(0, true);
                longRow.deserialise(shortRow.serialise(0));
                expectEquals(longRow.numSteps(), 16);
                expectEquals(longRow.numPages(), 1);
                expect(selected > longRow.numSteps() - 1, "the old selection is now past the end");
                expectEquals(juce::jmin(selected, longRow.numSteps() - 1), 15);
                expect(! longRow.isOn(60), "the loaded row does not carry the old row's hidden step");
            }

            beginTest("Length and a step on page 4 survive a save and load");
            {
                StepRow a;
                a.setNumSteps(50);
                a.setOn(49, true);
                StepRow b;
                b.deserialise(a.serialise(3));
                expectEquals(b.numSteps(), 50);
                expectEquals(b.numPages(), 4);
                expect(b.isOn(49), "the step on the last page");
            }

            beginTest("Tie at a page edge: step 16 is tied from 15 only when both are on and the flag is set");
            {
                StepRow row;
                row.setNumSteps(32);
                row.setOn(15, true);
                row.setOn(16, true);
                expect(! row.tiedFrom(16), "no tie flag yet");
                row.setTied(16, true);
                expect(row.tiedFrom(16), "a tie across the page edge (the stub case on page 2's first cell)");
                row.setOn(15, false);
                expect(! row.tiedFrom(16), "step 15 going off ends the tie into 16");
                expect(! row.isTied(16), "and clears its flag");
            }

            beginTest("Tie at a page edge: step 0 is never tied from anything, and a page start with no tie shows none");
            {
                StepRow row;
                row.setOn(0, true);
                row.setTied(0, true);
                expect(! row.tiedFrom(0), "there is no step before 0 to tie from");
                row.setNumSteps(32);
                row.setOn(16, true);
                row.setTied(16, true);                       // flag set but step 15 is off
                expect(! row.tiedFrom(16), "a flag without a lit step before it is not drawn as a tie");
            }

            beginTest("A tie across the page edge survives a save and load");
            {
                StepRow a;
                a.setNumSteps(32);
                a.setOn(15, true);
                a.setOn(16, true);
                a.setTied(16, true);
                StepRow b;
                b.deserialise(a.serialise(0));
                expect(b.tiedFrom(16), "the tie at the 15 -> 16 edge");
                expect(! b.tiedFrom(15), "and no tie where there was none");
            }
        }
    };

    static StepPagingTest stepPagingTest;
}
