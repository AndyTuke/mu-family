// A step's repeats, Level and Fade: how many extra hits follow the first, the note-value gap between them, the Fade across them, and
// the Level that scales the step. The arithmetic is tested directly, then through the clock (the event a step hands the Track),
// the save, and a real Track playing a pass and counting its hits.

#include "Plugin/PluginProcessor.h"
#include "Tests/ProcessorSeamHelpers.h"
#include "Sequencer/StepClock.h"
#include "Sequencer/StepRepeats.h"

namespace
{
    using mu_on::StepClock;
    using mu_on::StepEvent;
    using mu_on::StepRow;
    namespace rep = mu_on::step_repeats;

    class StepRepeatTest : public juce::UnitTest
    {
    public:
        StepRepeatTest() : juce::UnitTest("mu-on step repeats, Level and Fade", "Sequencer") {}

        void runTest() override
        {
            beginTest("the rates are note values in beats");
            {
                expectEquals(rep::rateBeats(3), 0.25);   // 1/16
                expectEquals(rep::rateBeats(0), 1.0);    // 1/4
                expectWithinAbsoluteError(rep::rateBeats(2), 1.0 / 3.0, 1.0e-12);   // 1/8T
                expectEquals(rep::rateBeats(7), 0.0625); // 1/64
                expectEquals(juce::String(rep::rateName(4)), juce::String("1/16T"));
            }

            beginTest("Fade: negative fades the repeats out, positive fades the first hit in, zero is flat");
            {
                for (int k = 0; k <= 4; ++k) expectEquals(rep::gain(k, 4, 0), 1.0f);
                expectEquals(rep::gain(0, 4, -100), 1.0f);
                expectEquals(rep::gain(4, 4, -100), 0.0f);
                expectWithinAbsoluteError(rep::gain(2, 4, -50), 0.75f, 1.0e-6f);    // halfway from 1 to 0.5
                expectEquals(rep::gain(0, 4, 100), 0.0f);
                expectEquals(rep::gain(4, 4, 100), 1.0f);
                expectWithinAbsoluteError(rep::gain(1, 4, 50), 0.625f, 1.0e-6f);    // from 0.5 up to 1
                expectEquals(rep::gain(0, 0, -100), 1.0f);   // no repeats: untouched
            }

            beginTest("repeats are planned at the rate and stop before the next step");
            {
                double beats[rep::kMaxRepeats];
                expectEquals(rep::plan(2.0, 3, 0.25, 100.0, beats), 3);
                expectEquals(beats[0], 2.25);  expectEquals(beats[2], 2.75);
                expectEquals(rep::plan(2.0, 16, 0.25, 3.0, beats), 3, "the one that would land on the next step is cut");
                expectEquals(rep::plan(2.0, 5, 0.5, 2.4, beats), 0, "nothing fits before a step that close");
                expectEquals(rep::plan(2.0, 40, 0.0625, 100.0, beats), (int) rep::kMaxRepeats, "never more than 16");
            }

            beginTest("a step's Level scales its velocity and its repeat settings ride on the event");
            {
                StepRow row;
                for (int s = 0; s < StepRow::kMaxSteps; ++s) row.setOn(s, false);
                row.setOn(0, true);  row.setOn(8, true);
                row.setStepLevel(8, 50);
                row.setStepRepeats(0, 3);  row.setStepRepeatRate(0, 5);  row.setStepRepeatFade(0, -40);
                std::vector<StepEvent> events;
                StepClock clock;
                clock.processSpan(row, 0.0, 96000, 120.0 / 60.0 / 48000.0, 0.0f, 0.5f, [&](const StepEvent& e) { events.push_back(e); });
                expectEquals((int) events.size(), 2);
                expectWithinAbsoluteError(events[1].velocity, events[0].velocity * 0.5f, 1.0e-6f);
                expectEquals(events[0].repeats, 3);
                expectEquals(events[0].repeatBeats, 0.125);
                expectEquals(events[0].repeatFade, -40);
                expectEquals(events[0].limitBeat, 2.0, "the next step that is on is step 8, at beat 2");
                expectEquals(events[1].repeats, 0);
            }

            beginTest("Level, repeats, rate and fade survive a save; a row left at the defaults writes none of them");
            {
                StepRow r;
                r.setStepLevel(2, 40);
                r.setStepRepeats(5, 7);  r.setStepRepeatRate(5, 6);  r.setStepRepeatFade(5, 60);
                StepRow loaded;
                loaded.deserialise(r.serialise(0));
                expectEquals(loaded.stepLevel(2), 40);
                expectEquals(loaded.stepRepeats(5), 7);
                expectEquals(loaded.stepRepeatRate(5), 6);
                expectEquals(loaded.stepRepeatFade(5), 60);
                expectEquals(loaded.stepLevel(0), 100);
                expectEquals(loaded.stepRepeats(0), 0);
                expectEquals(loaded.stepRepeatRate(0), (int) rep::kDefaultRate);
                StepRow plain;
                const auto saved = plain.serialise(0);
                expect(! saved.hasProperty("level") && ! saved.hasProperty("repeats") && ! saved.hasProperty("repRate") && ! saved.hasProperty("repFade"));
            }

            beginTest("the values are held to their ranges");
            {
                StepRow r;
                r.setStepLevel(0, 500);   expectEquals(r.stepLevel(0), 100);
                r.setStepLevel(0, -5);    expectEquals(r.stepLevel(0), 0);
                r.setStepRepeats(0, 99);  expectEquals(r.stepRepeats(0), 16);
                r.setStepRepeatRate(0, 50); expectEquals(r.stepRepeatRate(0), rep::kNumRates - 1);
                r.setStepRepeatFade(0, -300); expectEquals(r.stepRepeatFade(0), -100);
            }

            beginTest("a Track plays a step's repeats as extra hits, and a following step cuts the run");
            {
                mu_seam_test::TempStorage storage;
                mu_on::PluginProcessor p;
                auto& track = p.getTrack(0);
                auto& row = track.row;
                const double bpm = 120.0, sr = 48000.0, bps = bpm / 60.0 / sr;
                constexpr int kBlock = 512;
                auto hitsInPass = [&]
                {
                    const int before = track.triggers.load();
                    juce::AudioBuffer<float> out(2, kBlock);
                    double beat = 0.0;
                    const int blocks = (int) std::floor(4.0 / (bps * kBlock));   // just under one pass of 16 sixteenths (four beats): the next pass's first step stays out
                    for (int b = 0; b < blocks; ++b)
                    {
                        auto ctx = mu_core::makeRenderContext();
                        ctx.sampleRate = sr;  ctx.blockSize = kBlock;  ctx.preparedBlockSize = kBlock;  ctx.bpm = bpm;
                        ctx.startBeat = beat;  ctx.beatsPerSample = bps;  ctx.callBeats = bps * kBlock;
                        ctx.playing = 1;  ctx.justStarted = b == 0 ? 1 : 0;
                        out.clear();
                        track.render(ctx, out, kBlock);
                        beat += bps * kBlock;
                    }
                    return track.triggers.load() - before;
                };
                for (int s = 0; s < StepRow::kMaxSteps; ++s) row.setOn(s, false);
                row.setOn(0, true);
                expectEquals(hitsInPass(), 1, "one step, no repeats");
                row.setStepRepeats(0, 3);                                   // at 1/16 each
                expectEquals(hitsInPass(), 4, "the first hit and three repeats");
                row.setOn(2, true);                                         // a step half a beat in: repeats of step 0 end before it
                row.setStepRepeats(0, 8);
                expectEquals(hitsInPass(), 2 + 1, "step 0 plays 0 and 0.25 beats, step 2 at 0.5 cuts the rest, and step 2 itself plays once");
            }
        }
    };

    StepRepeatTest stepRepeatTest;
}
