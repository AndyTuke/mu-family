// Unit tests for the 909 sequencer: beat->step mapping, step-edge trigger firing,
// swing, accent velocity, pattern serialise/deserialise round-trip, and length wrap.

#include <juce_data_structures/juce_data_structures.h>
#include <vector>
#include "Sequencer/StepPattern.h"
#include "Tests/GrooveSequencer.h"

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
        beginTest("blocks with unequal spans (a MIDI clock being phase-corrected) fire every sixteenth exactly once");
        {
            // Each block advances the beat by its own span (tempo x length +/- 12 %), and the next block
            // starts exactly where it ended. Using the span keeps the blocks contiguous: no step falls in
            // a gap (dropped) or in two blocks (doubled).
            StepPattern p;
            for (int s = 0; s < StepPattern::kNumSteps; ++s) p.setOn(2, s, true);   // a hat on every sixteenth
            GrooveSequencer seq(p);
            seq.prepare(48000.0);
            seq.reset();

            juce::Random rng(99);
            const double nominal = 126.0 / 60.0 / 48000.0;
            double beat = 0.0;
            std::vector<int> fired(64, 0);   // fires of the hat per global sixteenth
            for (int b = 0; b < 20000 && beat < 15.99; ++b)
            {
                const int n = 64;
                const double bps = nominal * (0.88 + 0.24 * rng.nextDouble());
                const double blockStart = beat;
                seq.processSpan(beat, n, bps, [&](int track, float, int offset)
                {
                    // The step this hit belongs to, from the beat it was placed at in the block.
                    if (track == 2) ++fired[(size_t) juce::jlimit(0, 63, (int) std::lround((blockStart + offset * bps) / 0.25))];
                });
                beat += bps * n;
            }
            int wrong = 0;
            for (int s = 0; s < 60; ++s) if (fired[(size_t) s] != 1) ++wrong;   // the sixteenths before beat ~15
            expectEquals(wrong, 0, "sixteenths that fired zero or several times");
        }

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

        beginTest("the Accent knob maps to 0.72 + knob x 0.28, so an accent is never quieter than a plain step");
        {
            for (const float knob : { 0.0f, 0.25f, 0.5f, 0.72f, 1.0f })
            {
                StepPattern p;
                p.setOn(0, 0, true); p.setAccent(0, 0, true);
                GrooveSequencer seq(p);
                seq.prepare(48000.0);
                seq.setAccentVelocity(knob);
                auto b = run(seq, 0.0, 12000, 120.0);
                expect(! b.empty(), "the accented step fired");
                expectWithinAbsoluteError(b[0].vel, 0.72f + knob * 0.28f, 1.0e-6f);
                expect(b[0].vel >= StepClock::kNormalVelocity - 1.0e-6f, "never below a plain step");
                expect(StepClock::accentedVelocity(1.0f) == 1.0f, "a full Accent knob is full velocity");
            }
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

        beginTest("seek: a restart inside the first sixteenth fires step 0 again");
        {
            StepPattern p;
            p.setOn(0, 0, true);                                  // a kick on step 0 only
            GrooveSequencer seq(p);
            seq.prepare(48000.0);

            // 2400 samples at 120 BPM = 0.1 beat: step 0 fires and the sequencer waits for step 1.
            expectEquals((int) run(seq, 0.0, 2400, 120.0).size(), 1, "the first play fires step 0");
            // Stop and play again within the same sixteenth. Without a seek the sequencer still waits for step 1
            // (inside the resync window) and the new play loses its first step.
            expectEquals((int) run(seq, 0.0, 2400, 120.0).size(), 0, "premise: without a seek the replay drops step 0");
            seq.seek(0.0);
            const auto replay = run(seq, 0.0, 2400, 120.0);
            expectEquals((int) replay.size(), 1, "after seek(0) the replay fires step 0");
            if (! replay.empty()) expectEquals(replay[0].offset, 0, "on the first sample");
        }

        beginTest("seek: a start part-way through a swung sixteenth fires the step that lies ahead, once");
        {
            StepPattern p;
            p.setOn(0, 1, true);                                  // only step 1, an odd sixteenth
            GrooveSequencer seq(p);
            seq.prepare(48000.0);
            seq.setSwing(0.5f);                                   // step 1 fires at 0.25 + 0.5 * 0.5 * 0.25 = 0.3125 beat

            // Play the first half beat (fires step 1 at 0.3125); the sequencer now waits for step 2.
            expectEquals((int) run(seq, 0.0, 12000, 120.0).size(), 1, "the first play fires step 1");

            // Transport restarts at beat 0.3, inside step 1, 0.0125 beat before its swung start.
            seq.seek(0.3);
            const auto hits = run(seq, 0.3, 2400, 120.0);         // covers [0.3, 0.4)
            expectEquals((int) hits.size(), 1, "step 1 fires once after the restart");
            if (! hits.empty())
            {
                // 0.0125 beat at 120 BPM and 48 kHz is 300 samples.
                expect(std::abs(hits[0].offset - 300) <= 2, "at its swung position (offset " + juce::String(hits[0].offset) + ")");
            }
        }

        beginTest("a block start an ulp past the previous block's end does not skip the step on the boundary");
        {
            StepPattern p;
            p.setOn(0, 4, true);                                  // beat 1.0
            GrooveSequencer seq(p);
            seq.prepare(48000.0);

            // 1 / 4096 is exact in binary, so the first block ends on beat 1.0 exactly and step 4 (t = 1.0) is not yet inside it.
            constexpr double bps = 1.0 / 4096.0;
            int fired = 0;
            auto count = [&fired](int, float, int) { ++fired; };
            seq.processSpan(0.0, 4096, bps, count);
            expectEquals(fired, 0, "step 4 is not in the first block, which ends at beat 1.0");

            // The host reports the next block starting 1e-13 beat after where the last one ended (rounding in ppq).
            seq.processSpan(1.0 + 1.0e-13, 4096, bps, count);
            expectEquals(fired, 1, "step 4 fires once, in the second block");
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

        beginTest("a row longer than 16 steps wraps at its length and round-trips with it");
        {
            StepRow row;
            row.setNumSteps(32);
            row.setOn(0, true); row.setOn(20, true); row.setOn(31, true);
            expectEquals(row.numPages(), 2);
            expectEquals(StepClock::currentStep(7.75, 32), 31);   // 31 sixteenths in
            expectEquals(StepClock::currentStep(8.0, 32),  0);    // wraps after 8 beats, not 4

            // Two bars (8 beats) in one span: step 0, 20 and 31 each fire once, in order.
            StepClock clock;
            std::vector<int> steps;
            clock.processSpan(row, 0.0, 96000, 8.0 / 96000.0, 0.0f, 1.0f,
                              [&](const StepEvent& ev) { steps.push_back(ev.step); });
            expectEquals((int) steps.size(), 3, "one hit per on-step");
            if (steps.size() == 3) { expectEquals(steps[0], 0); expectEquals(steps[1], 20); expectEquals(steps[2], 31); }

            StepRow loaded;
            loaded.deserialise(row.serialise(0));
            expectEquals(loaded.numSteps(), 32);
            expect(loaded.isOn(20) && loaded.isOn(31) && ! loaded.isOn(21), "steps survive the round-trip");

            StepRow plain;   // a 16-step row writes no length, and reads back as 16
            expect(! plain.serialise(0).hasProperty("len"), "the default length is not saved");
        }
    }
};

static SequencerTest sequencerTest;
