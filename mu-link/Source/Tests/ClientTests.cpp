// Tests for the Stage L4 mu-core MuLinkClient — the product-facing client lifted into
// mu-core. Drives a real producer thread against an in-process ServerEngine: attach →
// the client renders ahead into its ring → the server consumes + sums it → transport is
// read back → clean detach frees the slot. Also covers the no-server fallback.

#include <juce_core/juce_core.h>
#include <vector>
#include "../Server/ServerEngine.h"
#include "../Ipc/MuLinkServerMemory.h"
#include "Link/MuLinkClient.h"

#ifdef _WIN32

using namespace mu_link;

namespace
{
    // float*[] over per-channel buffers for renderBlock.
    struct OutBuf
    {
        OutBuf(int ch, int n) : storage((std::size_t) ch, std::vector<float>((std::size_t) n, -99.0f))
        { for (auto& c : storage) ptrs.push_back(c.data()); }
        float* const* data() { return ptrs.data(); }
        std::vector<std::vector<float>> storage;
        std::vector<float*>             ptrs;
    };
}

class MuLinkClientTest : public juce::UnitTest
{
public:
    MuLinkClientTest() : juce::UnitTest("mu-link Client / MuLinkClient") {}

    void runTest() override
    {
        beginTest("attach fails cleanly when mu-link is not running");
        {
            MuLinkClient client;
            expect(! client.attach("orphan", 2), "attached with no server present");
            expect(! client.isAttached(), "isAttached true after a failed attach");
        }

        beginTest("client renders ahead; server consumes and sums it; transport reads back");
        {
            // Server side.
            MuLinkServerMemory mem;
            expect(mem.create(), "server memory failed");
            ServerEngine engine;
            engine.attachMemory(&mem);
            engine.prepare(48000.0, 256, 120.0);
            engine.setPlaying(true);

            // Client renders a constant 0.5 into every channel.
            MuLinkClient client;
            client.onRender([] (float* const* out, int numChannels, int numFrames, const TransportSnapshot&)
            {
                for (int c = 0; c < numChannels; ++c)
                    for (int i = 0; i < numFrames; ++i)
                        out[c][i] = 0.5f;
            });
            expect(client.attach("client-a", 2), "attach to running server failed");
            expectEquals(client.slotIndex(), 0);
            expectEquals((int) mem.registry().slots[0].active.load(), 1);
            expect(juce::String(mem.registry().slots[0].name) == "client-a", "registry name not written");

            // Wait for the producer thread to fill at least one block.
            expect(waitForFrames(mem, 0, 256, 3000), "client never produced audio into the ring");

            OutBuf out(2, 256);
            const auto stats = engine.renderBlock(out.data(), 2, 256);
            expectEquals(stats.activeClients, 1);

            bool summed = true;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 256; ++i)
                    if (out.storage[(std::size_t) c][(std::size_t) i] != 0.5f) summed = false;
            expect(summed, "server did not sum the client's rendered audio");

            // The server published the transport in renderBlock; the client reads it back.
            const auto snap = client.transport();
            expectEquals((int) snap.sampleRate, 48000);
            expectEquals((int) snap.blockSize, 256);
            expectEquals((int) snap.playing, 1);

            client.detach();
            expect(! client.isAttached(), "still attached after detach");
            expectEquals((int) mem.registry().slots[0].active.load(), 0);   // slot freed
        }

        beginTest("a detached slot can be re-claimed by a new client");
        {
            MuLinkServerMemory mem;
            expect(mem.create(), "server memory failed");

            MuLinkClient first;
            first.onRender([] (float* const*, int, int, const TransportSnapshot&) {});
            expect(first.attach("first", 2), "first attach failed");
            expectEquals(first.slotIndex(), 0);
            first.detach();

            MuLinkClient second;
            second.onRender([] (float* const*, int, int, const TransportSnapshot&) {});
            expect(second.attach("second", 2), "second attach failed");
            expectEquals(second.slotIndex(), 0);            // slot 0 reused
            expect(juce::String(mem.registry().slots[0].name) == "second", "registry name not updated on re-claim");
            second.detach();
        }
    }

private:
    // Spin until slot `slot`'s ring holds ≥ `want` frames, or timeout.
    static bool waitForFrames(MuLinkServerMemory& mem, int slot, int want, int timeoutMs)
    {
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
        while (juce::Time::getMillisecondCounter() < end)
        {
            if (mem.ring(slot).readAvailable() >= want)
                return true;
            juce::Thread::sleep(2);
        }
        return mem.ring(slot).readAvailable() >= want;
    }
};

static MuLinkClientTest muLinkClientTest;

#endif // _WIN32
