#pragma once

#include "Link/MuLinkProtocol.h"
#include "Link/MuLinkSharedMemory.h"

#include <juce_core/juce_core.h>

#include <functional>
#include <vector>

// MuLinkClient — the product-facing mu-link client, shared by every standalone.
//
// **Standalone-only.** Products wire this into the Standalone build only, gated on
// getWrapperType() == wrapperType_Standalone — VST3/CLAP never attach (the host owns the
// clock + device). See docs/mu-link/design-mulink.md §3.3.
//
// When mu-link is running, attach() auto-detects it (probing the shared-memory names),
// claims a registry slot, and starts a dedicated high-priority thread that renders the
// product's audio AHEAD into the shared ring (the async double-buffered model, design
// §3.2) — keeping the ring topped up so the server never underruns. The product supplies
// the audio via onRender and reads the master transport via transport() to slave its
// own clock. Absent / incompatible / full mu-link → attach() returns false and the
// product simply stays on its own audio device (mu-link is additive, never required).
//
// This generalises the mu-link-tone reference producer; lives in mu-core so all products
// share one implementation.
namespace mu_link
{

#ifdef _WIN32

class MuLinkClient : private juce::Thread
{
public:
    MuLinkClient() : juce::Thread("mu-link client") {}
    ~MuLinkClient() override { detach(); }

    // Render callback (called on the producer thread): fill `output` (per-channel,
    // numChannels × numFrames) for the block that begins at `transport.samplePos`. Must
    // be real-time-safe — no locks, no allocation. numChannels is the ring's channel
    // count (kMaxChannels); a mono product writes the same signal to both.
    using RenderCallback = std::function<void(float* const* output, int numChannels,
                                              int numFrames, const TransportSnapshot& transport)>;
    void onRender(RenderCallback cb) { renderCb = std::move(cb); }

    // Try to attach to a running mu-link. `numChannels` is recorded in the registry
    // (informational). Returns false if mu-link isn't running, the protocol version
    // mismatches, or every slot is taken — the product then runs on its own device.
    //
    // `startRendering` defaults to true (the producer thread starts immediately). Pass
    // false when the caller must finish wiring before audio flows — e.g. re-prepare the
    // processor at mu-link's sample rate — then call start() once ready.
    bool attach(const juce::String& displayName, int numChannels, bool startRendering = true)
    {
        if (attached)
            return true;
        if (! mem.open())
            return false;
        if (mem.claimSlot(displayName.toRawUTF8(), numChannels) < 0)
        {
            mem = MuLinkClientMemory{};   // no free slot — release the mapping
            return false;
        }
        ringChannels = juce::jlimit(1, kMaxChannels, mem.ring().numChannels());
        attached = true;
        if (startRendering)
            start();
        return true;
    }

    // Begin the producer thread (no-op if already running). Pairs with attach(..., false).
    void start()
    {
        if (attached && ! isThreadRunning())
            startThread(juce::Thread::Priority::high);
    }

    // Stop the producer thread and release the slot. Safe to call when not attached.
    void detach()
    {
        if (! attached)
            return;
        signalThreadShouldExit();
        notify();                 // wake the render loop out of its wait()
        stopThread(2000);
        mem.releaseSlot();
        attached = false;
    }

    bool isAttached()  const noexcept { return attached; }
    int  slotIndex()   const noexcept { return mem.claimedSlotIndex(); }

    // The current master transport snapshot — for slaving the product's transport. Zeroed
    // when not attached.
    TransportSnapshot transport() const noexcept
    {
        return attached ? readTransport(mem.transport()) : TransportSnapshot{};
    }

    // Raw seqlock generation of the transport block — advances every server block (even
    // while stopped). A frozen generation across polls means mu-link has stopped rendering
    // (quit, or closed its device); the product uses this to fall back. 0 when detached.
    std::uint64_t transportGeneration() const noexcept
    {
        return attached ? mem.transport().generation.load(std::memory_order_acquire) : 0;
    }

private:
    static constexpr int kRenderChunk = 512;   // frames rendered per fill pass

    // Producer loop: keep the ring full. Each pass renders into per-channel scratch via
    // onRender, interleaves to the ring layout, and writes whatever free space allows;
    // then bumps the heartbeat and briefly waits. Running ahead by up to the ring depth.
    void run() override
    {
        AudioRingView ring = mem.ring();
        const int rc = ringChannels;

        std::vector<std::vector<float>> chans((std::size_t) rc, std::vector<float>((std::size_t) kRenderChunk, 0.0f));
        std::vector<float*>             chanPtrs((std::size_t) rc);
        for (int c = 0; c < rc; ++c) chanPtrs[(std::size_t) c] = chans[(std::size_t) c].data();
        std::vector<float>              interleaved((std::size_t) kRenderChunk * (std::size_t) rc);

        while (! threadShouldExit())
        {
            int space = ring.writeAvailable();
            while (space > 0 && ! threadShouldExit())
            {
                const int n = juce::jmin(space, kRenderChunk);

                // Consume-time transport: this look-ahead block plays AFTER everything
                // already queued, so project the master position forward by the buffered
                // frames. That keeps a render-ahead client sample-accurately in sync —
                // the block carries the position at which mu-link will actually play it.
                TransportSnapshot snap = readTransport(mem.transport());
                snap.samplePos += (std::uint64_t) ring.readAvailable();

                for (int c = 0; c < rc; ++c)
                    std::fill(chanPtrs[(std::size_t) c], chanPtrs[(std::size_t) c] + n, 0.0f);

                if (renderCb)
                    renderCb(chanPtrs.data(), rc, n, snap);

                for (int i = 0; i < n; ++i)
                    for (int c = 0; c < rc; ++c)
                        interleaved[(std::size_t) i * (std::size_t) rc + (std::size_t) c] = chanPtrs[(std::size_t) c][i];

                const int wrote = ring.writeFrames(interleaved.data(), n);
                space -= wrote;
                if (wrote < n)
                    break;   // ring full for now
            }

            mem.bumpHeartbeat();
            wait(2);          // ~2 ms between fills (woken early by notify() on detach)
        }
    }

    MuLinkClientMemory mem;
    RenderCallback     renderCb;
    bool               attached     = false;
    int                ringChannels = kMaxChannels;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MuLinkClient)
};

#endif // _WIN32

} // namespace mu_link
