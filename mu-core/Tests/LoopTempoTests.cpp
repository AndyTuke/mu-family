// The tempo guess from a loop's length (Analysis/LoopTempo.h): a whole number of beats, the first count that lands in 75 to 165 BPM.

#include "Analysis/LoopTempo.h"

#include <juce_core/juce_core.h>

#include <vector>

struct LoopTempoTests : public juce::UnitTest
{
    LoopTempoTests() : juce::UnitTest("Loop tempo guess", "mu-core") {}

    void runTest() override
    {
        beginTest("a loop's tempo is guessed from its length as a whole number of beats");
        {
            expectWithinAbsoluteError(mu_core::estimateTempoFromLength(12.0), 80.0, 1.0e-9);    // 16 beats (4 bars) at 80
            expectWithinAbsoluteError(mu_core::estimateTempoFromLength(2.0), 120.0, 1.0e-9);    // 4 beats
            expectWithinAbsoluteError(mu_core::estimateTempoFromLength(7.5), 128.0, 1.0e-9);    // 16 beats at 128
            expectWithinAbsoluteError(mu_core::estimateTempoFromLength(1.5), 160.0, 1.0e-9);    // 4 beats at 160
            expectEquals(mu_core::estimateTempoFromLength(0.2), 0.0);                           // a one-shot: no tempo
            expectEquals(mu_core::estimateTempoFromLength(0.0), 0.0);
        }

        beginTest("the range edges and bad lengths");
        {
            expectEquals(mu_core::estimateTempoFromLength(-1.0), 0.0);
            expectEquals(mu_core::estimateTempoFromLength(std::nan("")), 0.0);
            expectWithinAbsoluteError(mu_core::estimateTempoFromLength(3.0), 80.0, 1.0e-9);   // 8 beats at 160 is also 3.0 s: the slower reading wins
            expectEquals(mu_core::estimateTempoFromLength(1000.0), 0.0);                       // longer than 64 beats can ever fit
        }

        beginTest("the attacks choose between the readings a length allows");
        {
            // 3.0 s is 8 beats at 160 or 4 beats at 80: hits every 0.375 s sit on the 160 grid but only half of them on the 80 grid.
            std::vector<double> fast;
            for (int i = 0; i < 8; ++i) fast.push_back(0.375 * i + 0.002);
            expectWithinAbsoluteError(mu_core::estimateTempoFromOnsets(3.0, fast), 160.0, 1.0e-9);

            // The same length with hits only on the quarter-length beats of 80: the slower reading stays (a tie goes slow).
            std::vector<double> slow { 0.0, 0.75, 1.5, 2.25 };
            expectWithinAbsoluteError(mu_core::estimateTempoFromOnsets(3.0, slow), 80.0, 1.0e-9);

            // Two bars of 3/4 (6 beats) in 2.5 s with a hit on every beat: the 4/4 reading (4 beats, 96) fits a third of them, the 3/4 one (144) all.
            std::vector<double> waltz;
            for (int i = 0; i < 6; ++i) waltz.push_back((2.5 / 6.0) * i);
            expectWithinAbsoluteError(mu_core::estimateTempoFromOnsets(2.5, waltz), 144.0, 1.0e-9);

            // Too few attacks: the length-only answer.
            expectWithinAbsoluteError(mu_core::estimateTempoFromOnsets(3.0, { 0.1, 0.4 }), 80.0, 1.0e-9);
            expectEquals(mu_core::estimateTempoFromOnsets(0.2, fast), 0.0);
        }
    }
};

static LoopTempoTests loopTempoTests;
