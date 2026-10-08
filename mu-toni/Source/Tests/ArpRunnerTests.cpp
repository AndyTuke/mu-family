// mu-toni arp runner tests — step timing. In loop mode the steps sit on the transport's beat grid
// (so the arp locks to a host's bars and lands on the same step after a jump); in MIDI-trigger
// mode the pattern restarts at each key press.

#include <juce_core/juce_core.h>
#include "Sequencer/ArpVoiceRunner.h"

using namespace mu_toni;

namespace
{
constexpr double kSr    = 48000.0;
constexpr int    kBlock = 512;
constexpr double kBpm   = 120.0;
constexpr int    kRate16th = 6;                                    // 1/16 = a quarter beat
constexpr double kBps   = (kBpm / 60.0) / kSr;                     // beats per sample
constexpr double kStepSamples = 0.25 / kBps;                       // 6000 samples per 1/16

ArpContext playingAt(double startBeat)
{
    ArpContext c;
    c.playing = true; c.sampleRate = kSr; c.bpm = kBpm;
    c.startBeat = startBeat; c.beatsPerSample = kBps;
    return c;
}
} // namespace

class ArpRunnerTest : public juce::UnitTest
{
public:
    ArpRunnerTest() : juce::UnitTest("Arp runner timing", "mu-toni") {}

    void runTest() override
    {
        mu_wavetable::WavetableBank bank;
        bank.loadFactoryBank();
        juce::AudioBuffer<float> buf(2, kBlock);

        auto makeRunner = [&bank](ArpVoiceRunner& r, bool midiTrigger)
        {
            r.setBank(&bank);
            r.prepare(kSr, kBlock);
            r.setStep(kRate16th, 0.5f, midiTrigger);
        };

        beginTest("Loop mode: every step lands on its beat-grid sample, numbered by the beat");
        {
            ArpVoiceRunner r; makeRunner(r, false);
            int fired = 0;
            double beat = 0.0;
            for (int b = 0; b < 200; ++b)   // ~2.1 s → 17 sixteenths
            {
                r.render(buf, kBlock, playingAt(beat));
                if (r.stepsFired() != fired)
                {
                    fired = r.stepsFired();
                    const long long sample = (long long) b * kBlock + r.lastStepOffset();
                    const int k = r.lastStep();
                    expectEquals((int) sample, (int) std::ceil(k * kStepSamples - 1.0e-9));
                    expectEquals(k, fired - 1);
                }
                beat += kBps * kBlock;
            }
            expectEquals(fired, (int) std::floor(200.0 * kBlock / kStepSamples) + 1);
        }

        beginTest("Loop mode: a jump in the beat (a DAW loop / locate) lands on that position's step");
        {
            ArpVoiceRunner r; makeRunner(r, false);
            for (int b = 0; b < 20; ++b) r.render(buf, kBlock, playingAt(b * kBps * kBlock));
            r.render(buf, kBlock, playingAt(8.0));    // locate to beat 8 = step 32
            expectEquals(r.lastStep(), 32);
            expectEquals(r.lastStepOffset(), 0);
            r.render(buf, kBlock, playingAt(1.1));    // loop back: the next grid step is 5 (beat 1.25)
            int guard = 0;
            double beat = 1.1 + kBps * kBlock;
            while (r.lastStep() != 5 && guard++ < 50) { r.render(buf, kBlock, playingAt(beat)); beat += kBps * kBlock; }
            expectEquals(r.lastStep(), 5);
        }

        beginTest("MIDI-trigger mode: a key press restarts the pattern at step 0 on its block");
        {
            ArpVoiceRunner r; makeRunner(r, true);
            auto ctx = playingAt(3.37);   // the beat doesn't matter in trigger mode
            ctx.anyNoteHeld = true; ctx.noteOnEdge = true;
            r.render(buf, kBlock, ctx);
            expectEquals(r.lastStep(), 0);
            expectEquals(r.lastStepOffset(), 0);
            ctx.noteOnEdge = false;
            int fired = r.stepsFired();
            int blocks = 1;
            while (r.stepsFired() == fired && blocks < 100) { r.render(buf, kBlock, ctx); ++blocks; }
            expectEquals(r.lastStep(), 1);
            expectEquals((blocks - 1) * kBlock + r.lastStepOffset(), (int) (kStepSamples));
        }
    }
};

static ArpRunnerTest arpRunnerTest;
