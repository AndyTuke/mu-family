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
            MidiClockSync sync;
            sync.setEnabled(true);
            Feeder f { sync };
            f.run(120.0, 48, true);
            expectWithinAbsoluteError (sync.getBeatPosUI(), 2.0, 1.0e-9, "48 pulses = 2 beats");
        }
    }
};

static MidiClockSyncTest midiClockSyncTest;
