// RhythmCircle: pointing the ring at another rhythm while stopped must show that rhythm's own current step,
// not the highlight left over from the rhythm shown before.

#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/RhythmCircle.h"

class RhythmCircleStepTest : public juce::UnitTest
{
public:
    RhythmCircleStepTest() : juce::UnitTest ("Rhythm circle step highlight", "UI") {}

    void runTest() override
    {
        beginTest ("switching rhythm while stopped resets the highlighted step to the new rhythm's");
        {
            mu_core::RhythmPlayState first, second;
            first.publishPosition ({ 5, 5, 0.25f });
            second.publishPosition ({ 2, 2, 0.5f });
            std::atomic<bool> playing { false };

            mu_ui::RhythmCircle circle;
            circle.setPlayState (&first, &playing, juce::Colours::red);
            expectEquals (circle.getDisplayedStep(), 5, "shows the first rhythm's step");

            circle.setPlayState (&second, &playing, juce::Colours::blue);
            expectEquals (circle.getDisplayedStep(), 2, "shows the second rhythm's step after the switch");

            circle.setPlayState (nullptr, &playing, juce::Colours::blue);
            expectEquals (circle.getDisplayedStep(), 0, "no rhythm shows step 0");
        }
    }
};

static RhythmCircleStepTest rhythmCircleStepTest;
