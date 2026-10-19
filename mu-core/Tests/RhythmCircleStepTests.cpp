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

        beginTest ("a play-state source is asked on every read: a moved or replaced play state is never kept");
        {
            mu_core::RhythmPlayState first, second;
            first.publishPosition ({ 5, 5, 0.25f });
            second.publishPosition ({ 2, 2, 0.5f });
            std::atomic<bool> playing { false };
            mu_core::RhythmPlayState* current = &first;

            mu_ui::RhythmCircle circle;
            circle.setPlayStateSource ([&current] { return current; }, &playing, juce::Colours::red);
            expect (circle.currentPlayState() == &first, "reads the source's play state");
            expectEquals (circle.getDisplayedStep(), 5, "shows its step at once");

            current = &second;   // the host's layer moved or was replaced
            expect (circle.currentPlayState() == &second, "follows the source without a rebind");

            current = nullptr;   // the slot is no longer live
            expect (circle.currentPlayState() == nullptr, "a slot with no layer reads none");

            circle.setPlayState (&first, &playing, juce::Colours::red);
            current = &second;
            expect (circle.currentPlayState() == &first, "a fixed pointer drops the source");
        }
    }
};

static RhythmCircleStepTest rhythmCircleStepTest;
