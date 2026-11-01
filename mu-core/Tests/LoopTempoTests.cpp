// The tempo guess from a loop's length (Analysis/LoopTempo.h): a whole number of beats, the first count that lands in 75 to 165 BPM.

#include "Analysis/LoopTempo.h"

#include <juce_core/juce_core.h>

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
    }
};

static LoopTempoTests loopTempoTests;
