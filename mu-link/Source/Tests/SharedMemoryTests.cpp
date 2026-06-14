// Tests for the Stage L1 Win32 shared-memory bus: named mappings for the TransportBlock,
// the ClientRegistry, and per-slot AudioRings. Two layers of coverage:
//   • in-process - server + client composers map the SAME named kernel objects through
//     independent MapViewOfFile views, proving the cast + atomic visibility + ring SPSC
//     all work across separate mappings (not just separate pointers into one buffer);
//   • cross-process - a child mu-link-tests process attaches as a real client, verifies
//     the published transport, and pushes a ring payload the parent reads back.

#include <juce_core/juce_core.h>
#include "../Ipc/MuLinkServerMemory.h"
#include "Link/MuLinkSharedMemory.h"
#include "ShmTestVectors.h"

#ifdef _WIN32

using namespace mu_link;

class SharedMemoryTest : public juce::UnitTest
{
public:
    SharedMemoryTest() : juce::UnitTest("mu-link IPC / SharedMemoryBus") {}

    void runTest() override
    {
        beginTest("client.open() fails when no server is running");
        {
            // Nothing has created the regions in this scope, so attaching must fail and
            // the client falls back to its own device.
            MuLinkClientMemory client;
            expect(! client.open(), "client attached with no mu-link present");
        }

        beginTest("transport published by the server is read back through an independent mapping");
        {
            MuLinkServerMemory server;
            expect(server.create(), "server failed to create regions");

            auto& t = server.transport();
            t.samplePos .store(mu_link_test::kSamplePos,  std::memory_order_relaxed);
            t.tempoBpm  .store(mu_link_test::kTempo,      std::memory_order_relaxed);
            t.sampleRate.store(mu_link_test::kSampleRate, std::memory_order_relaxed);
            t.blockSize .store(mu_link_test::kBlockSize,  std::memory_order_relaxed);
            t.playing   .store(1,                         std::memory_order_relaxed);
            t.generation.store(mu_link_test::kGeneration, std::memory_order_release);

            MuLinkClientMemory client;
            expect(client.open(), "client failed to attach to a running server");

            const auto& ct = client.transport();
            expectEquals((int) ct.samplePos.load(),  (int) mu_link_test::kSamplePos);
            expect(ct.tempoBpm.load() == mu_link_test::kTempo, "tempo mismatch");
            expectEquals((int) ct.sampleRate.load(), (int) mu_link_test::kSampleRate);
            expectEquals((int) ct.blockSize.load(),  (int) mu_link_test::kBlockSize);
            expectEquals((int) ct.playing.load(),    1);
            expectEquals((int) ct.generation.load(), (int) mu_link_test::kGeneration);
        }

        beginTest("claimSlot registers in the shared registry and releaseSlot frees it");
        {
            MuLinkServerMemory server;
            expect(server.create(), "server failed to create regions");

            MuLinkClientMemory client;
            expect(client.open(), "client failed to attach");

            const int slot = client.claimSlot("voice-a", 2);
            expectEquals(slot, 0);                       // first free slot
            auto& reg = server.registry();
            expectEquals((int) reg.slots[0].active.load(), 1);
            expectEquals((int) reg.slots[0].numChannels.load(), 2);
            expect(juce::String(reg.slots[0].name) == "voice-a", "registry name not written");

            client.releaseSlot();
            expectEquals((int) reg.slots[0].active.load(), 0);
        }

        beginTest("ring round-trips frames in order across independent mappings");
        {
            MuLinkServerMemory server;
            expect(server.create(), "server failed to create regions");
            MuLinkClientMemory client;
            expect(client.open(), "client failed to attach");
            const int slot = client.claimSlot("ring-test", mu_link_test::kRingChans);
            expectEquals(slot, 0);

            float in[mu_link_test::kRingFrames * mu_link_test::kRingChans];
            for (int i = 0; i < mu_link_test::kRingFrames; ++i)
                for (int c = 0; c < mu_link_test::kRingChans; ++c)
                    in[i * mu_link_test::kRingChans + c] = mu_link_test::ringSample(i, c);

            expectEquals(client.ring().writeFrames(in, mu_link_test::kRingFrames), mu_link_test::kRingFrames);

            float out[mu_link_test::kRingFrames * mu_link_test::kRingChans] = {};
            expectEquals(server.ring(slot).readFrames(out, mu_link_test::kRingFrames), mu_link_test::kRingFrames);

            bool ok = true;
            for (int i = 0; i < mu_link_test::kRingFrames * mu_link_test::kRingChans; ++i)
                if (out[i] != in[i]) ok = false;
            expect(ok, "ring payload corrupted across the mapping");
        }

        beginTest("a protocol-version mismatch is refused");
        {
            MuLinkServerMemory server;
            expect(server.create(), "server failed to create regions");
            // Simulate an incompatible server by poisoning the published version.
            server.registry().protocolVersion.store(kProtocolVersion + 1000, std::memory_order_release);

            MuLinkClientMemory client;
            expect(! client.open(), "client attached despite a protocol-version mismatch");

            server.registry().protocolVersion.store(kProtocolVersion, std::memory_order_release);
        }

        beginTest("cross-process: child client reads the transport and produces a ring the parent consumes");
        {
            MuLinkServerMemory server;
            expect(server.create(), "server failed to create regions");

            auto& t = server.transport();
            t.samplePos .store(mu_link_test::kSamplePos,  std::memory_order_relaxed);
            t.tempoBpm  .store(mu_link_test::kTempo,      std::memory_order_relaxed);
            t.sampleRate.store(mu_link_test::kSampleRate, std::memory_order_relaxed);
            t.blockSize .store(mu_link_test::kBlockSize,  std::memory_order_relaxed);
            t.playing   .store(1,                         std::memory_order_relaxed);
            t.generation.store(mu_link_test::kGeneration, std::memory_order_release);

            const auto exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
            juce::StringArray args;
            args.add(exe.getFullPathName());
            args.add("--shm-child");

            juce::ChildProcess child;
            const bool started = child.start(args);
            expect(started, "failed to spawn the child test process");

            if (started)
            {
                const bool finished = child.waitForProcessToFinish(15000);
                expect(finished, "child process timed out");
                expectEquals((int) child.getExitCode(), 0);   // 0 = child validated everything

                // The child claimed slot 0 and pushed the payload; read it back here.
                auto& reg = server.registry();
                expectEquals((int) reg.slots[0].active.load(), 1);
                expect(juce::String(reg.slots[0].name) == mu_link_test::kClientName, "child name not in registry");

                float out[mu_link_test::kRingFrames * mu_link_test::kRingChans] = {};
                const int got = server.ring(0).readFrames(out, mu_link_test::kRingFrames);
                expectEquals(got, mu_link_test::kRingFrames);

                bool ok = true;
                for (int i = 0; i < mu_link_test::kRingFrames; ++i)
                    for (int c = 0; c < mu_link_test::kRingChans; ++c)
                        if (out[i * mu_link_test::kRingChans + c] != mu_link_test::ringSample(i, c)) ok = false;
                expect(ok, "cross-process ring payload corrupted");
            }
        }
    }
};

static SharedMemoryTest sharedMemoryTest;

#endif // _WIN32
