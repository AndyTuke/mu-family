// Shared NudgeInput (mu-core): whole numbers behave as ever; the decimals mode (the BPM field,
// "127.5") rounds, clamps, shows its decimals, and notifies only on a real change.

#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/Components/NudgeInput.h"

class NudgeInputTest : public juce::UnitTest
{
public:
    NudgeInputTest() : juce::UnitTest("NudgeInput", "UI") {}

    void runTest() override
    {
        beginTest("Whole numbers: clamp, int callback, no change = no notify");
        {
            NudgeInput n("x", 20, 300, 120);
            int calls = 0, last = 0;
            n.onChange = [&](int v) { ++calls; last = v; };
            n.setValue(500, true);
            expectEquals(n.getValue(), 300, "clamped to the maximum");
            expectEquals(last, 300);
            n.setValue(300, true);
            expectEquals(calls, 1, "an unchanged value does not notify again");
            n.setValue(12, true);
            expectEquals(n.getValue(), 20, "clamped to the minimum");
        }

        beginTest("Decimals mode: rounds to the shown precision and fires the double callback");
        {
            NudgeInput n("BPM", 20, 300, 120);
            n.setDecimals(1);
            int intCalls = 0;
            double last = 0.0;
            int dCalls = 0;
            n.onChange  = [&](int) { ++intCalls; };
            n.onChangeD = [&](double v) { ++dCalls; last = v; };
            n.setValueD(127.46, true);
            expectWithinAbsoluteError(n.getValueD(), 127.5, 1.0e-9, "rounded to a tenth");
            expectWithinAbsoluteError(last, 127.5, 1.0e-9);
            n.setValueD(127.52, true);                       // rounds to the same 127.5
            expectEquals(dCalls, 1, "a value that rounds to the same tenth does not notify");
            n.setValueD(1000.0, true);
            expectWithinAbsoluteError(n.getValueD(), 300.0, 1.0e-9, "clamped");
            expectEquals(intCalls, 0, "the int callback stays quiet in decimals mode");
        }

        beginTest("Decimals mode: arrow click steps a whole BPM and keeps the fraction, Shift a tenth");
        {
            NudgeInput n("BPM", 20, 300, 120);
            n.setDecimals(1);
            n.setShowStepButtons(false);
            n.setStep(1.0);
            n.setFineStep(0.1);
            n.setSize(80, 24);
            n.setValueD(127.5);
            // The up arrow sits in the top-right; a click there steps by the base step.
            const auto upArrow = juce::Point<float>((float) n.getWidth() - 4.0f, 3.0f);
            const auto plain = juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), upArrow, juce::ModifierKeys(),
                                                0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &n, &n, juce::Time::getCurrentTime(), upArrow,
                                                juce::Time::getCurrentTime(), 1, false);
            n.mouseDown(plain);
            expectWithinAbsoluteError(n.getValueD(), 128.5, 1.0e-9, "plain click: +1.0, fraction kept");
            const auto shift = juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), upArrow,
                                                juce::ModifierKeys(juce::ModifierKeys::shiftModifier),
                                                0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &n, &n, juce::Time::getCurrentTime(), upArrow,
                                                juce::Time::getCurrentTime(), 1, false);
            n.mouseDown(shift);
            expectWithinAbsoluteError(n.getValueD(), 128.6, 1.0e-9, "Shift click: +0.1");
        }
    }
};

static NudgeInputTest nudgeInputTest;
