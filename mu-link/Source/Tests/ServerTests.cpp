// Tests for the Stage L2 ServerEngine — the headless real-time core: transport publish
// (read back through the seqlock snapshot), lock-free summing of client rings, underrun
// zero-fill, and the 24-ppqn MIDI-clock pulse math. The device-owning AudioServer is a
// thin shell over this and is exercised by the runnable mu-link-server app, not here.

#include <juce_core/juce_core.h>
#include <vector>
#include "../Server/ServerEngine.h"
#include "../Ipc/MuLinkServerMemory.h"
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

            produce(a, 64, [](int i, int c) { return (float) (i * 2 + c); });
            produce(b, 64, [](int i, int c) { return 100.0f + (float) (i * 2 + c); });

            OutputBuffers out(2, 64);
            auto st = engine.renderBlock(out.data(), 2, 64);
            expectEquals(st.activeClients, 2);
            expectEquals(st.underrunFrames, 0);

            bool ok = true;
            for (int i = 0; i < 64; ++i)
                for (int c = 0; c < 2; ++c)
                {
                    const float expected = 100.0f + 2.0f * (float) (i * 2 + c);
                    if (out.storage[(std::size_t) c][(std::size_t) i] != expected) ok = false;
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
            produce(a, 10, [](int, int) { return 1.0f; });   // only 10 of 64 frames available

            OutputBuffers out(2, 64);
            auto st = engine.renderBlock(out.data(), 2, 64);
            expectEquals(st.underrunFrames, (64 - 10));

            bool headOk = true, tailSilent = true;
            for (int i = 0; i < 64; ++i)
                for (int c = 0; c < 2; ++c)
                {
                    const float v = out.storage[(std::size_t) c][(std::size_t) i];
                    if (i < 10) { if (v != 1.0f) headOk = false; }
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
    }
};

static ServerEngineTest serverEngineTest;

#endif // _WIN32
