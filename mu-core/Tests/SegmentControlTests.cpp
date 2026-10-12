#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/Components/SegmentControl.h"

// SegmentControl selection notifications, including the opt-in re-select firing.
struct SegmentControlTests : juce::UnitTest
{
    SegmentControlTests() : juce::UnitTest("SegmentControl selection", "mu-core") {}

    void runTest() override
    {
        beginTest("default: re-selecting the current segment does not notify");
        {
            SegmentControl sc({ "A", "B", "C" });
            int fired = 0;
            sc.onChange = [&](int) { ++fired; };
            sc.setSelectedIndex(1, true);
            expectEquals(fired, 1);
            sc.setSelectedIndex(1, true);
            expectEquals(fired, 1);
            expect(! sc.getFiresOnReselect());
        }

        beginTest("opt-in: re-selecting the current segment notifies again");
        {
            SegmentControl sc({ "A", "B", "C" });
            sc.setFiresOnReselect(true);
            int fired = 0, last = -1;
            sc.onChange = [&](int i) { ++fired; last = i; };
            sc.setSelectedIndex(2, true);
            sc.setSelectedIndex(2, true);
            expectEquals(fired, 2);
            expectEquals(last, 2);
        }

        beginTest("opt-in: a silent re-select (notify=false) stays silent");
        {
            SegmentControl sc({ "A", "B" });
            sc.setFiresOnReselect(true);
            int fired = 0;
            sc.onChange = [&](int) { ++fired; };
            sc.setSelectedIndex(0, false);
            expectEquals(fired, 0);
        }

        beginTest("a click on the selected segment follows the opt-in");
        {
            SegmentControl sc({ "A", "B" });
            sc.setBounds(0, 0, 100, 20);
            int fired = 0;
            sc.onChange = [&](int) { ++fired; };
            auto click = [&sc](int x)
            {
                const juce::MouseEvent e(juce::Desktop::getInstance().getMainMouseSource(),
                                         juce::Point<float>((float) x, 5.0f), juce::ModifierKeys(), 0.0f, 0.0f, 0.0f,
                                         0.0f, 0.0f, &sc, &sc, juce::Time::getCurrentTime(),
                                         juce::Point<float>((float) x, 5.0f), juce::Time::getCurrentTime(), 1, false);
                sc.mouseDown(e);
            };
            click(75);   // B
            expectEquals(fired, 1);
            click(75);
            expectEquals(fired, 1);
            sc.setFiresOnReselect(true);
            click(75);
            expectEquals(fired, 2);
        }
    }
};

static SegmentControlTests segmentControlTests;
