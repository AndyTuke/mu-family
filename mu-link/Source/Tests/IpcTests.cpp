// Unit tests for the mu-link IPC foundation: the lock-free SPSC AudioRing and the
// sample-accurate TransportClock. These are the correctness-critical primitives the
// whole product rests on, so they're pinned before any audio-server wiring lands.

#include <juce_core/juce_core.h>
#include <thread>
#include <vector>
#include "../Ipc/AudioRing.h"
#include "../Clock/TransportClock.h"

using namespace mu_link;

class AudioRingTest : public juce::UnitTest
{
public:
    AudioRingTest() : juce::UnitTest("mu-link IPC / AudioRing") {}

    void runTest() override
    {
        beginTest("round-trips interleaved frames intact");
        {
            AudioRing ring;
            ring.prepare(2, 64);
            expectEquals(ring.numChannels(), 2);
            expectEquals(ring.writeAvailable(), 64);
            expectEquals(ring.readAvailable(), 0);

            // 4 stereo frames: L = i, R = i + 0.5
            std::vector<float> in { 0,0.5f, 1,1.5f, 2,2.5f, 3,3.5f };
            expectEquals(ring.writeFrames(in.data(), 4), 4);
            expectEquals(ring.readAvailable(), 4);

            std::vector<float> out(8, -1.0f);
            expectEquals(ring.readFrames(out.data(), 4), 4);
            for (int i = 0; i < 8; ++i) expect(out[(size_t) i] == in[(size_t) i], "sample mismatch");
            expectEquals(ring.readAvailable(), 0);
        }

        beginTest("short write when the ring is full");
        {
            AudioRing ring;
            ring.prepare(1, 8);
            std::vector<float> in(16, 1.0f);
            expectEquals(ring.writeFrames(in.data(), 16), 8);   // only 8 fit
            expectEquals(ring.writeFrames(in.data(), 4), 0);    // now completely full
        }

        beginTest("short read signals underrun");
        {
            AudioRing ring;
            ring.prepare(1, 8);
            std::vector<float> in { 1, 2, 3 };
            ring.writeFrames(in.data(), 3);
            std::vector<float> out(8, 0.0f);
            expectEquals(ring.readFrames(out.data(), 8), 3);    // only 3 available → underrun
        }

        beginTest("wrap-around preserves data across the capacity boundary");
        {
            AudioRing ring;
            ring.prepare(1, 8);
            float v = 0.0f;
            // Push/pull 5 at a time, 40 times — forces many wraps; verify FIFO order.
            float expected = 0.0f;
            for (int iter = 0; iter < 40; ++iter)
            {
                float chunk[5];
                for (int i = 0; i < 5; ++i) chunk[i] = v++;
                expectEquals(ring.writeFrames(chunk, 5), 5);

                float out[5] = { -1,-1,-1,-1,-1 };
                expectEquals(ring.readFrames(out, 5), 5);
                for (int i = 0; i < 5; ++i) { expect(out[i] == expected, "FIFO order broken on wrap"); expected += 1.0f; }
            }
        }

        beginTest("concurrent producer/consumer transfers every frame in order");
        {
            AudioRing ring;
            ring.prepare(1, 1024);
            constexpr int total = 200000;

            std::thread producer([&]
            {
                int written = 0;
                float v = 0.0f;
                while (written < total)
                {
                    float chunk[64];
                    const int n = juce::jmin(64, total - written);
                    for (int i = 0; i < n; ++i) chunk[i] = v + (float) i;
                    int done = 0;
                    while (done < n) done += ring.writeFrames(chunk + done, n - done);   // spin if full
                    v += (float) n; written += n;
                }
            });

            int read = 0; bool ordered = true; float expected = 0.0f;
            while (read < total)
            {
                float chunk[64];
                const int n = ring.readFrames(chunk, 64);
                for (int i = 0; i < n; ++i) { if (chunk[i] != expected) ordered = false; expected += 1.0f; }
                read += n;
            }
            producer.join();
            expect(ordered, "SPSC concurrent transfer lost or reordered frames");
            expectEquals(read, total);
        }
    }
};

class TransportClockTest : public juce::UnitTest
{
public:
    TransportClockTest() : juce::UnitTest("mu-link IPC / TransportClock") {}

    void runTest() override
    {
        beginTest("stopped clock holds position");
        {
            TransportClock c; c.prepare(48000.0, 120.0);
            c.advance(4800);                       // not playing
            expectEquals((int) c.samplePosition(), 0);
        }

        beginTest("120 BPM → 1 beat after half a second");
        {
            TransportClock c; c.prepare(48000.0, 120.0); c.setPlaying(true);
            c.advance(24000);                      // 0.5 s at 48 kHz
            expectWithinAbsoluteError(c.beats(), 1.0, 1.0e-9);   // 2 beats/sec × 0.5 s
            expectEquals((int) c.samplePosition(), 24000);
        }

        beginTest("bar phase wraps over 4 beats");
        {
            TransportClock c; c.prepare(48000.0, 120.0); c.setPlaying(true);
            c.advance(24000 * 4);                  // exactly 4 beats → bar boundary
            expectWithinAbsoluteError(c.barPhase(4.0), 0.0, 1.0e-9);
            c.advance(24000);                      // +1 beat into the next bar
            expectWithinAbsoluteError(c.barPhase(4.0), 0.25, 1.0e-9);
        }

        beginTest("24 ppqn MIDI-clock pulses track beats");
        {
            TransportClock c; c.prepare(48000.0, 120.0); c.setPlaying(true);
            c.advance(24000);                      // 1 beat
            expectEquals((int) c.pulsesElapsed(24), 24);
            c.advance(24000);                      // 2 beats
            expectEquals((int) c.pulsesElapsed(24), 48);
        }
    }
};

static AudioRingTest      audioRingTest;
static TransportClockTest transportClockTest;
