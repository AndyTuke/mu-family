// The StatusBar's CPU readout (docs/design-ui-family.md section 6): whole percent from a load fraction, amber from 70 and
// red from 90 on the shown number, opt-in and off by default, the links keeping clear of the readout, and at most four
// repaints a second so the digits stay readable. Driven without a display.

#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/Components/StatusBar.h"

class StatusBarCpuTests : public juce::UnitTest
{
public:
    StatusBarCpuTests() : juce::UnitTest("Status bar: the CPU readout", "mu-core") {}

    void runTest() override
    {
        using Level = StatusBar::CpuLevel;

        beginTest("whole percent, rounded, never negative, and over 100 it simply reads over 100");
        expectEquals(StatusBar::cpuPercentFor(0.0f), 0);
        expectEquals(StatusBar::cpuPercentFor(0.234f), 23);
        expectEquals(StatusBar::cpuPercentFor(0.236f), 24);
        expectEquals(StatusBar::cpuPercentFor(1.0f), 100);
        expectEquals(StatusBar::cpuPercentFor(1.12f), 112);
        expectEquals(StatusBar::cpuPercentFor(-0.5f), 0);

        beginTest("normal below 70, amber from 70, red from 90, judged on the number that is shown");
        expect(StatusBar::cpuLevelFor(0.0f)   == Level::Normal);
        expect(StatusBar::cpuLevelFor(0.69f)  == Level::Normal);
        expect(StatusBar::cpuLevelFor(0.694f) == Level::Normal, "69 % still reads 69");
        expect(StatusBar::cpuLevelFor(0.70f)  == Level::Warn);
        expect(StatusBar::cpuLevelFor(0.897f) == Level::Hot, "it reads 90, so it is red");
        expect(StatusBar::cpuLevelFor(0.89f)  == Level::Warn);
        expect(StatusBar::cpuLevelFor(0.90f)  == Level::Hot);
        expect(StatusBar::cpuLevelFor(1.5f)   == Level::Hot);
        expectEquals(MuLookAndFeel::kCpuWarnAt, 0.70f);
        expectEquals(MuLookAndFeel::kCpuHotAt, 0.90f);
        expectEquals(MuLookAndFeel::kStatusCpuW, 64);

        beginTest("off by default; opt-in turns it on");
        {
            StatusBar bar;
            expect(! bar.isCpuMeterVisible());
            bar.setCpuMeterVisible(true);
            expect(bar.isCpuMeterVisible());
            bar.setCpuMeterVisible(false);
            expect(! bar.isCpuMeterVisible());
        }

        beginTest("the readout repaints at most four times a second: a change inside 250 ms waits for the next call");
        {
            StatusBar bar;
            bar.setBounds(0, 0, 600, 22);
            bar.setCpuMeterVisible(true);
            bar.setCpuLoad(0.20f);
            expectEquals(bar.getCpuPercent(), 20);
            bar.setCpuLoad(0.55f);   // straight after: too soon, the figure on screen does not change
            expectEquals(bar.getCpuPercent(), 20);
            juce::Thread::sleep(StatusBar::kCpuRepaintMs + 40);
            bar.setCpuLoad(0.55f);
            expectEquals(bar.getCpuPercent(), 55, "and it catches up on the next call after the interval");
            bar.setCpuLoad(0.55f);
            expectEquals(bar.getCpuPercent(), 55, "an unchanged figure changes nothing");
        }

        beginTest("with the readout off the figure is taken at once (nothing is painted, so nothing to throttle)");
        {
            StatusBar bar;
            bar.setCpuLoad(0.30f);
            bar.setCpuLoad(0.42f);
            expectEquals(bar.getCpuPercent(), 42);
        }

        beginTest("the Update and Support links shift left of the readout by its slot and a gap (70 at Medium)");
        {
            using LF = MuLookAndFeel;
            expectEquals(StatusBar::linksRightEdgeFor(800, false), 796);
            expectEquals(StatusBar::linksRightEdgeFor(800, false) - StatusBar::linksRightEdgeFor(800, true), LF::kStatusCpuW + LF::kSpaceS);
            expectEquals(StatusBar::linksRightEdgeFor(800, true), 726);
            expect(StatusBar::linksRightEdgeFor(800, true) <= 800 - LF::kSpaceS - LF::kStatusCpuW, "clear of the readout's slot");
        }
    }
};

static StatusBarCpuTests sStatusBarCpuTests;
