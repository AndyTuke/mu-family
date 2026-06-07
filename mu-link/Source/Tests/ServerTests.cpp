// Tests for the Stage L2 ServerEngine — the headless real-time core: transport publish
// (read back through the seqlock snapshot), lock-free summing of client rings, underrun
// zero-fill, and the 24-ppqn MIDI-clock pulse math. The device-owning AudioServer is a
// thin shell over this and is exercised by the runnable mu-link-server app, not here.

#include <juce_core/juce_core.h>
#include <vector>
#include "../Server/ServerEngine.h"
#include "../Ipc/MuLinkServerMemory.h"
#include "../Clock/MidiClockEstimator.h"
#include "Link/MuLinkSharedMemory.h"

#ifdef _WIN32

using namespace mu_link;

namespace
{
    // Build a float*[] over per-channel buffers for renderBlock.
    struct OutputBuffers
    {
        explicit OutputBuffers(int numChannels, int numFrames)
            : storage((std::size_t) numChannels, std::vector<float>((std::size_t) numFrames, -99.0f))
        {
            for (auto& ch : storage) ptrs.push_back(ch.data());
        }
        float* const* data() { return ptrs.data(); }
        std::vector<std::vector<float>> storage;
        std::vector<float*>             ptrs;
    };

    // Write `numFrames` stereo frames into a client's producer ring; value = gen(i, ch).
    template <typename Fn>
    void produce(MuLinkClientMemory& client, int numFrames, Fn gen)
    {
        std::vector<float> in((std::size_t) numFrames * (std::size_t) kMaxChannels);
        for (int i = 0; i < numFrames; ++i)
            for (int c = 0; c < kMaxChannels; ++c)
                in[(std::size_t) i * kMaxChannels + c] = gen(i, c);
        client.ring().writeFrames(in.data(), numFrames);
    }
}

class ServerEngineTest : public juce::UnitTest
{
public:
    ServerEngineTest() : juce::UnitTest("mu-link Server / ServerEngine") {}

    void runTest() override
    {
        beginTest("publishes a start-of-block transport readable through the seqlock");
        {
            MuLinkServerMemory mem;
            expect(mem.create(), "server memory failed");
            ServerEngine engine;
            engine.attachMemory(&mem);
            engine.prepare(48000.0, 256, 120.0);
            engine.setPlaying(true);

            OutputBuffers out(2, 256);
            engine.renderBlock(out.data(), 2, 256);

            auto s = readTransport(mem.transport());
            expectEquals((int) s.samplePos, 0);          // start-of-block position of block 1
            expectEquals((int) s.blockSize, 256);
            expectEquals((int) s.sampleRate, 48000);
            expectEquals((int) s.playing, 1);
            expect(s.tempoBpm == 120.0, "tempo not published");

            engine.renderBlock(out.data(), 2, 256);
            s = readTransport(mem.transport());
            expectEquals((int) s.samplePos, 256);        // advanced by one block
        }

        beginTest("a stopped transport holds position and emits no pulses");
        {
            MuLinkServerMemory mem;
            expect(mem.create(), "server memory failed");
            ServerEngine engine;
            engine.attachMemory(&mem);
            engine.prepare(48000.0, 24000, 120.0);
            engine.setPlaying(false);

            OutputBuffers out(2, 24000);
            auto st = engine.renderBlock(out.data(), 2, 24000);
            expectEquals((int) st.midiPulsesInBlock, 0);
            expectEquals((int) readTransport(mem.transport()).samplePos, 0);
        }

        beginTest("sums two client rings sample-accurately into the master");
        {
            MuLinkServerMemory mem;
            expect(mem.create(), "server memory failed");
            ServerEngine engine;
            engine.attachMemory(&mem);
            engine.prepare(48000.0, 64, 120.0);
            engine.setPlaying(true);

            MuLinkClientMemory a, b;
            expect(a.open() && b.open(), "clients failed to attach");
            expectEquals(a.claimSlot("a", 2), 0);
            expectEquals(b.claimSlot("b", 2), 1);

            // Sub-knee levels so the master soft-clip is transparent and the sum is exact.
            produce(a, 64, [](int i, int c) { return 0.001f * (float) (i * 2 + c); });
            produce(b, 64, [](int i, int c) { return 0.4f + 0.001f * (float) (i * 2 + c); });

            OutputBuffers out(2, 64);
            auto st = engine.renderBlock(out.data(), 2, 64);
            expectEquals(st.activeClients, 2);
            expectEquals(st.underrunFrames, 0);

            bool ok = true;
            for (int i = 0; i < 64; ++i)
                for (int c = 0; c < 2; ++c)
                {
                    const float expected = 0.4f + 0.002f * (float) (i * 2 + c);   // A + B
                    if (std::abs(out.storage[(std::size_t) c][(std::size_t) i] - expected) > 1.0e-5f) ok = false;
                }
            expect(ok, "summed master does not match A+B");
        }

        beginTest("underrun zero-fills the missing tail and is counted, never clicks");
        {
            MuLinkServerMemory mem;
            expect(mem.create(), "server memory failed");
            ServerEngine engine;
            engine.attachMemory(&mem);
            engine.prepare(48000.0, 64, 120.0);
            engine.setPlaying(true);

            MuLinkClientMemory a;
            expect(a.open(), "client failed to attach");
            expectEquals(a.claimSlot("short", 2), 0);
            produce(a, 10, [](int, int) { return 0.5f; });   // only 10 of 64 frames (sub-knee level)

            OutputBuffers out(2, 64);
            auto st = engine.renderBlock(out.data(), 2, 64);
            expectEquals(st.underrunFrames, (64 - 10));

            bool headOk = true, tailSilent = true;
            for (int i = 0; i < 64; ++i)
                for (int c = 0; c < 2; ++c)
                {
                    const float v = out.storage[(std::size_t) c][(std::size_t) i];
                    if (i < 10) { if (std::abs(v - 0.5f) > 1.0e-6f) headOk = false; }
                    else        { if (v != 0.0f) tailSilent = false; }
                }
            expect(headOk, "available frames not summed");
            expect(tailSilent, "underrun tail was not silent — would click the master");
        }

        beginTest("MIDI-clock pulses track the clock at 24 ppqn");
        {
            MuLinkServerMemory mem;
            expect(mem.create(), "server memory failed");
            ServerEngine engine;
            engine.attachMemory(&mem);
            engine.prepare(48000.0, 24000, 120.0);
            engine.setPlaying(true);

            OutputBuffers out(2, 24000);
            // 0.5 s at 120 BPM = 1 beat = 24 pulses.
            auto st = engine.renderBlock(out.data(), 2, 24000);
            expectEquals((int) st.midiPulsesInBlock, 24);
            // Next identical block crosses the next 24 pulses.
            st = engine.renderBlock(out.data(), 2, 24000);
            expectEquals((int) st.midiPulsesInBlock, 24);
        }

        beginTest("reaps a client whose heartbeat freezes (died without detaching)");
        {
            MuLinkServerMemory mem;
            expect(mem.create(), "server memory failed");
            ServerEngine engine;
            engine.attachMemory(&mem);
            engine.prepare(48000.0, 512, 120.0);
            engine.setPlaying(true);

            // Simulate a client that attached then died: slot active, heartbeat set once
            // and never advanced again.
            mem.registry().slots[0].active.store(1);
            mem.registry().slots[0].heartbeat.store(777);

            OutputBuffers out(2, 512);
            bool reaped = false;
            // Threshold is 2 s = 96000 frames ≈ 188 blocks of 512; run well past it.
            for (int b = 0; b < 300 && ! reaped; ++b)
                if (engine.renderBlock(out.data(), 2, 512).reapedClients > 0)
                    reaped = true;

            expect(reaped, "zombie slot was never reaped");
            expectEquals((int) mem.registry().slots[0].active.load(), 0);
        }

        beginTest("does not reap a client whose heartbeat keeps advancing");
        {
            MuLinkServerMemory mem;
            expect(mem.create(), "server memory failed");
            ServerEngine engine;
            engine.attachMemory(&mem);
            engine.prepare(48000.0, 512, 120.0);
            engine.setPlaying(true);

            mem.registry().slots[0].active.store(1);

            OutputBuffers out(2, 512);
            for (int b = 0; b < 300; ++b)
            {
                mem.registry().slots[0].heartbeat.store((std::uint64_t) (b + 1));   // alive
                engine.renderBlock(out.data(), 2, 512);
            }
            expectEquals((int) mem.registry().slots[0].active.load(), 1);   // never reaped
        }

        beginTest("mixer: per-client gain scales, mute silences");
        {
            MuLinkServerMemory mem;
            expect(mem.create(), "server memory failed");
            ServerEngine engine;
            engine.attachMemory(&mem);
            engine.prepare(48000.0, 64, 120.0);
            engine.setPlaying(true);

            // Slot 0 active with a constant 1.0 stereo block in its ring.
            mem.registry().slots[0].active.store(1);
            std::vector<float> in(64 * kMaxChannels, 1.0f);
            mem.ring(0).writeFrames(in.data(), 64);

            engine.setClientGain(0, 0.5f);
            OutputBuffers out(2, 64);
            engine.renderBlock(out.data(), 2, 64);
            expectWithinAbsoluteError(out.storage[0][0], 0.5f, 1.0e-6f);   // 1.0 × 0.5 gain

            // Refill (the block was consumed) and mute → silence.
            mem.ring(0).writeFrames(in.data(), 64);
            engine.setClientMute(0, true);
            engine.renderBlock(out.data(), 2, 64);
            expectWithinAbsoluteError(out.storage[0][0], 0.0f, 1.0e-6f);
        }

        beginTest("mixer: solo isolates the soloed client");
        {
            MuLinkServerMemory mem;
            expect(mem.create(), "server memory failed");
            ServerEngine engine;
            engine.attachMemory(&mem);
            engine.prepare(48000.0, 64, 120.0);
            engine.setPlaying(true);

            mem.registry().slots[0].active.store(1);
            mem.registry().slots[1].active.store(1);
            std::vector<float> a(64 * kMaxChannels, 1.0f), b(64 * kMaxChannels, 0.3f);
            mem.ring(0).writeFrames(a.data(), 64);
            mem.ring(1).writeFrames(b.data(), 64);

            engine.setClientSolo(1, true);   // only slot 1 audible
            OutputBuffers out(2, 64);
            engine.renderBlock(out.data(), 2, 64);
            expectWithinAbsoluteError(out.storage[0][0], 0.3f, 1.0e-6f);   // slot 0 excluded
        }

        beginTest("master safety soft-clip keeps a hot signal under full scale");
        {
            MuLinkServerMemory mem;
            expect(mem.create(), "server memory failed");
            ServerEngine engine;
            engine.attachMemory(&mem);
            engine.prepare(48000.0, 64, 120.0);
            engine.setPlaying(true);

            // A grossly over-unity client (2.0) must not produce > 1.0 on the master.
            mem.registry().slots[0].active.store(1);
            std::vector<float> hot(64 * kMaxChannels, 2.0f);
            mem.ring(0).writeFrames(hot.data(), 64);

            OutputBuffers out(2, 64);
            engine.renderBlock(out.data(), 2, 64);
            expect(out.storage[0][0] <= 1.0f, "soft-clip let the master exceed full scale");
            expect(out.storage[0][0] >  0.9f, "soft-clip should still pass near full scale");

            // And a normal-level signal (0.5) passes through untouched (below the knee).
            mem.registry().slots[0].active.store(0);
            for (int b = 0; b < 2; ++b) engine.renderBlock(out.data(), 2, 64);   // let slot meter fall
            mem.registry().slots[1].active.store(1);
            std::vector<float> norm(64 * kMaxChannels, 0.5f);
            mem.ring(1).writeFrames(norm.data(), 64);
            engine.renderBlock(out.data(), 2, 64);
            expectWithinAbsoluteError(out.storage[0][0], 0.5f, 1.0e-6f);   // transparent below knee
        }

        beginTest("MIDI clock estimator tracks tempo + transport (external-clock slave)");
        {
            MidiClockEstimator est;
            expect(! est.isRunning(), "should begin stopped");

            est.onStart();
            expect(est.isRunning(), "Start (0xFA) → running");
            expect(est.consumeReset(), "Start requests a position reset");
            expect(! est.consumeReset(), "reset is consumed exactly once");

            // 24-ppqn pulses at 120 BPM: quarter = 0.5 s, so a pulse every 0.5/24 s.
            double t = 0.0;
            const double pulse120 = 0.5 / 24.0;
            for (int i = 0; i < 80; ++i) { est.onClockPulse(t); t += pulse120; }
            expectWithinAbsoluteError(est.bpm(), 120.0, 0.5);

            // Speed up to 140 BPM — the smoothed estimate tracks toward it (jitter-free input).
            const double pulse140 = (60.0 / 140.0) / 24.0;
            for (int i = 0; i < 300; ++i) { est.onClockPulse(t); t += pulse140; }
            expectWithinAbsoluteError(est.bpm(), 140.0, 1.0);

            est.onStop();
            expect(! est.isRunning(), "Stop (0xFC) → stopped");
        }

        beginTest("renderBlock tolerates a block larger than the prepared max (#913)");
        {
            MuLinkServerMemory mem;
            expect(mem.create(), "server memory failed");
            ServerEngine engine;
            engine.attachMemory(&mem);
            engine.prepare(48000.0, 64, 120.0);   // de-interleave scratch sized for 64 frames
            engine.setPlaying(true);

            mem.registry().slots[0].active.store(1);
            std::vector<float> in(64 * kMaxChannels, 0.25f);
            mem.ring(0).writeFrames(in.data(), 64);

            // The device hands us a BIGGER block than was prepared — must not overrun the
            // scratch. The available client frames sum in; the rest stays clean silence.
            OutputBuffers out(2, 256);
            auto st = engine.renderBlock(out.data(), 2, 256);
            expectEquals(st.activeClients, 1);
            expectWithinAbsoluteError(out.storage[0][0],   0.25f, 1.0e-6f);
            expectWithinAbsoluteError(out.storage[0][120], 0.0f,  1.0e-6f);   // beyond the prepared max
        }

        beginTest("external MIDI clock: a stalled stream stops the transport (#912)");
        {
            MuLinkServerMemory mem;
            expect(mem.create(), "server memory failed");
            ServerEngine engine;
            engine.attachMemory(&mem);
            MidiClockEstimator est;
            engine.attachMidiClock(&est);
            engine.setClockSource(ClockSource::ExternalMidi);
            engine.prepare(48000.0, 512, 120.0);

            est.onStart();
            double t = 0.0; const double pulse = 0.5 / 24.0;   // 120 BPM
            for (int i = 0; i < 10; ++i) { est.onClockPulse(t); t += pulse; }

            OutputBuffers out(2, 512);
            engine.renderBlock(out.data(), 2, 512);            // fresh pulses → running
            expectEquals((int) readTransport(mem.transport()).playing, 1, "should be playing while clock is live");

            // No further pulses arrive (cable pulled, no 0xFC). After > 0.5 s of frames the
            // stall watchdog stops the transport rather than coasting forever at last tempo.
            for (int b = 0; b < 60; ++b) engine.renderBlock(out.data(), 2, 512);   // ≈ 0.64 s
            expectEquals((int) readTransport(mem.transport()).playing, 0, "stalled external clock should stop the transport");
        }
    }
};

static ServerEngineTest serverEngineTest;

#endif // _WIN32
