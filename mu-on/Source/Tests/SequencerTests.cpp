// Unit tests for the 909 sequencer: beat->step mapping, step-edge trigger firing,
// swing, accent velocity, pattern serialise/deserialise round-trip, and length wrap.

#include <juce_data_structures/juce_data_structures.h>
#include <vector>
#include "Sequencer/StepPattern.h"
#include "Sequencer/GrooveSequencer.h"

using namespace mu_on;

namespace
{
    struct Hit { int track; float vel; int offset; };

    // Run one block and collect the triggers it fires.
    std::vector<Hit> run(GrooveSequencer& seq, double beatStart, int numSamples, double bpm)
    {
        std::vector<Hit> hits;
        seq.process(beatStart, numSamples, bpm,
                    [&hits](int t, float v, int o) { hits.push_back({ t, v, o }); });
        return hits;
    }
}

class SequencerTest : public juce::UnitTest
{
public:
    SequencerTest() : juce::UnitTest("mu-on Sequencer", "Sequencer") {}

    void runTest() override
    {
        beginTest("beat -> step mapping wraps every 16 steps (4 beats)");
        {
            expectEquals(GrooveSequencer::currentStep(0.0),  0);
            expectEquals(GrooveSequencer::currentStep(0.25), 1);
            expectEquals(GrooveSequencer::currentStep(1.0),  4);
            expectEquals(GrooveSequencer::currentStep(3.75), 15);
            expectEquals(GrooveSequencer::currentStep(4.0),  0);   // wrap
            expectEquals(GrooveSequencer::currentStep(4.25), 1);
        }

        beginTest("fires exactly the on-steps across one bar, in order");
        {
            StepPattern p;
            p.setOn(0, 0, true); p.setOn(0, 4, true); p.setOn(0, 8, true); p.setOn(0, 12, true); // 4-on-floor
            GrooveSequencer seq(p);
            seq.prepare(48000.0);

            // One bar (4 beats) at 120 BPM = 0.5 s = 96000 samples - but the per-block guard
            // caps at 32 steps; process in four 1-beat blocks (12000 samples each).
            std::vector<Hit> all;
            double beat = 0.0;
            for (int b = 0; b < 4; ++b)
            {
                auto h = run(seq, beat, 12000, 120.0);
                for (auto& x : h) all.push_back(x);
                beat += 1.0;
            }
            expectEquals((int) all.size(), 4, "one kick per beat");
            for (auto& x : all) expectEquals(x.track, 0);
        }

        beginTest("accent cell fires at the accent velocity, plain cell at the normal velocity");
        {
            StepPattern p;
            p.setOn(0, 0, true); p.setAccent(0, 0, true);
            p.setOn(0, 4, true);                              // plain
            GrooveSequencer seq(p);
            seq.prepare(48000.0);
            seq.setAccentVelocity(1.0f);

            auto b0 = run(seq, 0.0, 12000, 120.0);            // step 0 (accent)
            auto b1 = run(seq, 1.0, 12000, 120.0);            // step 4 (plain)
            expect(! b0.empty() && ! b1.empty(), "both steps fired");
            expectWithinAbsoluteError(b0[0].vel, 1.0f, 1.0e-6f);     // accent
            expect(b1[0].vel < 0.9f, "plain step uses the lower normal velocity");
        }

        beginTest("swing delays the odd 16th");
        {
            auto fireBeatOfStep1 = [](float swing) -> double
            {
                StepPattern p; p.setOn(0, 1, true);          // only step 1 (an odd 16th)
                GrooveSequencer seq(p);
                seq.prepare(48000.0);
                seq.setSwing(swing);
                auto h = run(seq, 0.0, 24000, 120.0);        // covers beats [0, 1.0)
                if (h.empty()) return -1.0;
                return h[0].offset * (120.0 / 60.0) / 48000.0;   // offset -> beats
            };
            const double straight = fireBeatOfStep1(0.0f);
            const double swung    = fireBeatOfStep1(1.0f);
            expectWithinAbsoluteError(straight, 0.25, 1.0e-3);
            expect(swung > straight + 0.02, "swing should push the odd step later in the bar");
        }

        beginTest("pattern serialise -> deserialise round-trips on/accent");
        {
            StepPattern a;
            a.setOn(0, 0, true); a.setAccent(0, 0, true);
            a.setOn(1, 2, true); a.setOn(2, 5, true); a.setOn(3, 12, true); a.setAccent(3, 12, true);

            juce::ValueTree root("MuOnState");
            a.serialise(root);

            StepPattern b;
            b.deserialise(root);
            for (int t = 0; t < StepPattern::kNumTracks; ++t)
                for (int s = 0; s < StepPattern::kNumSteps; ++s)
                {
                    expect(b.isOn(t, s)     == a.isOn(t, s),     "on mismatch after round-trip");
                    expect(b.isAccent(t, s) == a.isAccent(t, s), "accent mismatch after round-trip");
                }
        }
    }
};

static SequencerTest sequencerTest;
