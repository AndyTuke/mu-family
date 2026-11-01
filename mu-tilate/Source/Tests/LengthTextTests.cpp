// The length box's text: what it accepts (bars, beats, note fractions, a bare number in the shown unit), what it refuses, and
// that what it shows reads back to the same length.

#include "UI/LengthText.h"

#include <juce_core/juce_core.h>

using namespace mu_tilate;

struct LengthTextTests : public juce::UnitTest
{
    LengthTextTests() : juce::UnitTest("Length text", "mu-tilate") {}

    void runTest() override
    {
        auto beats = [](const char* t, double bare = 1.0) { return parseLengthBeats(t, bare); };

        beginTest("bars, beats and note fractions");
        {
            expectEquals(*beats("2 bars"), 8.0);
            expectEquals(*beats("1 bar"), 4.0);
            expectEquals(*beats("6 beats"), 6.0);
            expectEquals(*beats("6b"), 6.0);
            expectEquals(*beats("2.5 beats"), 2.5);
            expectEquals(*beats("3/4"), 3.0);
            expectEquals(*beats("6/8"), 3.0);
            expectEquals(*beats("1/16"), 0.25);
            expectEquals(*beats("  4 BARS "), 16.0);
        }

        beginTest("a bare number is in the unit the box was showing");
        {
            expectEquals(*beats("3", 4.0), 12.0);   // the box showed bars
            expectEquals(*beats("3", 1.0), 3.0);    // the box showed beats
        }

        beginTest("nonsense and non-positive values are refused");
        {
            for (const char* t : { "", "abc", "0", "-2", "2 apples", "1.2.3", "/4", "3/0", "x/4" })
                expect(! beats(t).has_value(), juce::String("accepted: ") + t);
        }

        beginTest("what is shown reads back to the same length");
        {
            for (double b : { 1.0, 2.5, 3.0, 4.0, 12.0, 256.0 })
                expectEquals(*parseLengthBeats(formatLengthBeats(b), 1.0), b);
            expectEquals(formatLengthBeats(8.0), juce::String("2 bars"));
            expectEquals(formatLengthBeats(6.0), juce::String("6 beats"));
            expectEquals(formatLengthBeats(1.0), juce::String("1 beat"));
        }
    }
};

static LengthTextTests lengthTextTests;
