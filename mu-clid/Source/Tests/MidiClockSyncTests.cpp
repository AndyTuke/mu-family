// MIDI-clock slave tests: the shared mu-core MidiClockSync + MidiClockTempo estimator.
// Feeds synthetic 0xFA / 0xF8 streams block by block and checks the tempo estimate —
// in particular that it is right from the first ticks after a Start, not only once a
// window has filled.

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Plugin/MidiClockSync.h"
class MidiClockSyncTest : public juce::UnitTest
{
public:
    MidiClockSyncTest() : juce::UnitTest ("MIDI clock sync", "Transport") {}

    static constexpr double kSr    = 48000.0;
    static constexpr int    kBlock = 512;

    // Drives `sync` for `numTicks` clock pulses at `bpm` (optional Start first), with an
    // optional deterministic +/- jitter in samples on each pulse.
    struct Feeder
    {
        MidiClockSync& sync;
        juce::int64    nextTick   = 0;   // absolute sample of the next pulse
        juce::int64    blockStart = 0;
        int            tickIndex  = 0;

        void run(double bpm, int numTicks, bool sendStart, int jitter = 0)
        {
            const double tickSamples = kSr * 60.0 / (bpm * 24.0);
            if (sendStart) nextTick = blockStart;   // the Start + first pulse land in this block
            bool startPending = sendStart;
            int  sent = 0;
            const double base = (double) nextTick;

            // One block at a time: queue every pulse that falls inside it, then process.
            while (sent < numTicks)
            {
                juce::MidiBuffer midi;
                if (startPending) { midi.addEvent(juce::MidiMessage(0xFA), 0); startPending = false; }
                while (sent < numTicks)
                {
                    const int j = jitter == 0 ? 0 : ((tickIndex * 7919) % (2 * jitter + 1)) - jitter;
                    const juce::int64 at = (juce::int64) std::llround(base + sent * tickSamples) + j;
                    if (at >= blockStart + kBlock) break;
                    midi.addEvent(juce::MidiMessage(0xF8), (int) juce::jlimit<juce::int64>(0, kBlock - 1, at - blockStart));
                    ++sent; ++tickIndex;
                }
                sync.process(midi, kBlock, kSr);
                blockStart += kBlock;
            }
            nextTick = (juce::int64) std::llround(base + sent * tickSamples);
        }
    };

    void runTest() override
    {
        beginTest ("Cold start: tempo is right from the second pulse");
        {
            MidiClockSync sync;
            sync.setEnabled(true);
            Feeder f { sync };
            f.run(120.0, 2, true);
            expectWithinAbsoluteError (sync.getBpm(), 120.0, 0.5, "after 2 pulses");
            f.run(120.0, 10, false);
            expectWithinAbsoluteError (sync.getBpm(), 120.0, 0.5, "after 12 pulses");
            expect (sync.isPlaying(), "Start sets playing");
        }

        beginTest ("Restart at a new tempo converges within two beats");
        {
            MidiClockSync sync;
            sync.setEnabled(true);
            Feeder f { sync };
            f.run(150.0, 96, true);
            expectWithinAbsoluteError (sync.getBpm(), 150.0, 0.5, "steady at 150");
            f.run(90.0, 48, true);
            expectWithinAbsoluteError (sync.getBpm(), 90.0, 1.0, "two beats after restarting at 90");
        }

        beginTest ("Jittery clock: estimate stays close to the true tempo");
        {
            MidiClockSync sync;
            sync.setEnabled(true);
            Feeder f { sync };
            f.run(128.0, 192, true, 40);   // +/- 40 samples (~0.8 ms) per pulse
            expectWithinAbsoluteError (sync.getBpm(), 128.0, 1.5, "jittered 128");
        }

        beginTest ("Beat position advances one beat per 24 pulses");
        {
            // The first clock after Start is tick 0, so 48 clocks reach 47/24 beats; the UI beat
            // then runs on at the tempo to the end of the last block (at most one block further).
            MidiClockSync sync;
            sync.setEnabled(true);
            Feeder f { sync };
            f.run(120.0, 48, true);
            const double lastPulse = 47.0 / 24.0, blockBeats = kBlock / kSr * 2.0;
            expectGreaterOrEqual (sync.getBeatPosUI(), lastPulse - 0.005, "not behind the last clock");
            expectLessOrEqual    (sync.getBeatPosUI(), lastPulse + blockBeats + 0.005, "no further than a block past it");
        }

        beginTest ("Block-start beat tracks the true clock beat, steady and with jitter");
        {
            // Ground truth: the master's beat at each block start is the time since Start in
            // beats (the first clock, at the Start, is tick 0). The continuous beat must follow it
            // closely once the tempo estimate has settled, and never run backwards.
            for (const int jitter : { 0, 40 })
            {
                MidiClockSync sync;
                sync.setEnabled(true);
                const double bpm = 123.0;
                const double tick = kSr * 60.0 / (bpm * 24.0);
                juce::int64 n = 0, blockStart = 0;
                double prev = -1.0, worst = 0.0, worstSettled = 0.0;
                bool backwards = false;
                for (int b = 0; b < 2000; ++b, blockStart += kBlock)
                {
                    juce::MidiBuffer midi;
                    if (b == 0) midi.addEvent(juce::MidiMessage((juce::uint8) 0xFA), 0);
                    for (;; ++n)
                    {
                        const int j = jitter == 0 ? 0 : (int) ((n * 7919) % (2 * jitter + 1)) - jitter;
                        const juce::int64 at = juce::jmax<juce::int64>(0, (juce::int64) std::llround((double) n * tick) + j);
                        if (at >= blockStart + kBlock) break;
                        midi.addEvent(juce::MidiMessage((juce::uint8) 0xF8), (int) (at - blockStart));
                    }
                    const double beat  = sync.process(midi, kBlock, kSr);
                    const double truth = (double) blockStart / tick / 24.0;
                    if (beat < prev) backwards = true;
                    prev  = beat;
                    worst = juce::jmax(worst, std::abs(truth - beat));
                    if (b >= 40) worstSettled = juce::jmax(worstSettled, std::abs(truth - beat));
                }
                const auto label = "jitter " + juce::String(jitter);
                logMessage ("  " + label + ": worst beat error " + juce::String(worst, 5) + ", settled " + juce::String(worstSettled, 5));
                expect (! backwards, label + ": block-start beat went backwards");
                expectLessOrEqual (worst, 1.0 / 24.0, label + ": beat error vs the master, from the first block");
                expectLessOrEqual (worstSettled, jitter == 0 ? 0.0005 : 0.005, label + ": beat error vs the master once settled");
            }
        }

        beginTest ("Every 16th boundary lands exactly on its step over 4 bars");
        {
            // A clock on the first sample of a block puts that block's start exactly on the pulse's
            // beat: pulse index 6k is step k. Summing 1/24 per pulse used to drift below the boundary
            // so the step floor fired a pulse late; the sequencers' 1e-9 step tolerance covers the rest.
            MidiClockSync sync;
            sync.setEnabled(true);
            int wrong = 0;
            for (int p = 0; p < 4 * 4 * 24; ++p)
            {
                juce::MidiBuffer block;
                if (p == 0) block.addEvent(juce::MidiMessage((juce::uint8) 0xFA), 0);
                block.addEvent(juce::MidiMessage((juce::uint8) 0xF8), 0);
                const double beat = sync.process(block, kBlock, kSr);
                const int step = (int) ((beat + 1.0e-9) / 0.25);
                if (step != p / 6) ++wrong;
            }
            expectEquals (wrong, 0, "blocks that put the pulse on the wrong 16th step");
        }
    }
};

static MidiClockSyncTest midiClockSyncTest;
