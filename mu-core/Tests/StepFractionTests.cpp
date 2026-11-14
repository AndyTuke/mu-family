// The ring's step and fraction must agree: a block that starts a rounding hair below a step boundary is step N
// with fraction 0 (beatToStep's epsilon), never step N with a fraction near 1, which drew the ring a whole step
// ahead for one block.

#include <juce_core/juce_core.h>
#include "Sequencer/RhythmSequencer.h"

#include <cmath>

class StepFractionTest : public juce::UnitTest
{
public:
    StepFractionTest() : juce::UnitTest ("Step fraction agrees with the step", "Sequencer") {}

    void runTest() override
    {
        using mu_core::RhythmSequencer;
        constexpr double kStep = RhythmSequencer::StepLengthBeats;

        beginTest ("a beat a hair below a step boundary reads as the new step at fraction 0");
        {
            int bad = 0;
            for (int k = 1; k < 2000; ++k)
                for (const double hair : { 1e-16, 1e-13, 1e-11, 1e-10 })   // all inside the epsilon (2.5e-10 beats)
                {
                    const double beat = (double) k * kStep - hair;
                    if (RhythmSequencer::beatToStep (beat) != k || RhythmSequencer::stepFraction (beat) > 0.01f)
                        ++bad;
                }
            expectEquals (bad, 0);
        }

        beginTest ("the angle (step + fraction) never jumps between blocks that divide the step");
        {
            // 120 bpm at 48 kHz is 6000 samples per step: block sizes that divide it start blocks on the
            // boundaries, where the rounding hair decides which side a start falls.
            const double beatsPerSample = (120.0 / 60.0) / 48000.0;
            int jumps = 0;
            for (const int block : { 250, 500, 750, 1000, 1500, 2000, 3000, 6000 })
            {
                double beat  = 0.0;
                double prev  = 0.0;
                for (int n = 0; n < 4000; ++n)
                {
                    const double angle = (double) RhythmSequencer::beatToStep (beat)
                                       + (double) RhythmSequencer::stepFraction (beat);
                    if (n > 0 && std::abs (angle - prev) > 1.5 * block * beatsPerSample / kStep + 1e-3)
                        ++jumps;
                    prev  = angle;
                    beat += block * beatsPerSample;
                }
            }
            expectEquals (jumps, 0);
        }

        beginTest ("an ordinary mid-step beat keeps its fraction");
        {
            expectWithinAbsoluteError (RhythmSequencer::stepFraction (0.125), 0.5f, 1e-6f);
            expectWithinAbsoluteError (RhythmSequencer::stepFraction (1.0 + 0.0625), 0.25f, 1e-6f);
            expectEquals (RhythmSequencer::stepFraction (-1.0), 0.0f);
            expectEquals (RhythmSequencer::stepFraction (std::nan ("")), 0.0f);
        }
    }
};

static StepFractionTest stepFractionTest;
