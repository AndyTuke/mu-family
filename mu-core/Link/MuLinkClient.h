#pragma once

#include "Link/MuLinkProtocol.h"
#include "Link/MuLinkSharedMemory.h"

#include <juce_core/juce_core.h>

#include <functional>
#include <vector>
#include <cstring>

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

    // Inside the render callback: how many frames are already queued ahead of the block being
    // rendered, i.e. how long before it is heard (the render lead). Producer thread only.
    int renderLeadFrames() const noexcept { return currentLead; }

    // How far ahead of the server this client keeps its ring filled, in frames. The deeper it is,
    // the safer against a late render and the later Stop, mutes, knob moves and preset changes are
    // heard (lead ≈ depth). Never less than two server blocks (the server reads a block at a time),
    // and never more than the ring holds. Any thread; takes effect on the next fill pass.
    // Measured (ClientTests, MU_LATENCY_MEASURE=1): a depth rides out a render stall about as long as
    // itself — 1024 fails a 30 ms stall, 2048 a 60 ms one, 4096 survives both. 4096 (≈ 85 ms at 48 kHz,
    // half the old full-ring lead) is the safe default until mu-link offers a Latency choice.
    static constexpr int kDefaultTargetDepthFrames = kDefaultClientDepthFrames;
    void setTargetDepthFrames(int frames) noexcept
    {
        targetDepth.store(juce::jlimit(kRenderChunk, kRingCapacityFrames - kRenderChunk, frames), std::memory_order_relaxed);
    }
    int  getTargetDepthFrames() const noexcept     { return targetDepth.load(std::memory_order_relaxed); }

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
        lastPcEpoch  = 0;   // claimSlot reset the slot's pcEpoch to 0 — match it so no stale fire
        lastDepthRequest = 0;   // a request from before this attach is re-read and applied afresh
        defaultDepth = 0;
        slotLostFlag.store(false, std::memory_order_relaxed);
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

    // True once the producer found its slot gone (mu-link reaped it after a stall of two seconds or
    // more). The producer has stopped; the owner should detach() and, if it wants, attach() afresh.
    bool slotLost() const noexcept { return slotLostFlag.load(std::memory_order_relaxed); }

    // Publish the client's current preset name into its registry slot (display-only; mu-link's
    // UI shows it). No-op when detached. Call from the message thread on each preset load.
    void setPresetName(const juce::String& name) noexcept
    {
        if (! attached || ! mem.ownsSlot()) return;   // never write into a slot that is now someone else's
        const int slot = mem.claimedSlotIndex();
        auto& dst = mem.registry().slots[slot].presetName;
        const auto utf8 = name.toRawUTF8();
        const int n = juce::jmin((int) kMaxNameChars - 1, (int) std::strlen(utf8));
        std::memcpy(dst, utf8, (size_t) n);
        dst[n] = '\0';
    }

    // Frames the server reports this client's ring was short by since attach (underruns); a rising
    // count means the depth is too low. 0 when detached. Message thread (it reads the attach state).
    std::uint64_t underrunFrames() const noexcept
    {
        const int slot = attached ? mem.claimedSlotIndex() : -1;
        return slot >= 0 ? mem.registry().slots[slot].underrunFrames.load(std::memory_order_relaxed) : 0;
    }

    // The hardware output latency mu-link reports, in frames (0 = unknown or detached). Message thread.
    int outputLatencyFrames() const noexcept { return attached ? (int) mem.transport().outputLatencyFrames.load(std::memory_order_relaxed) : 0; }

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

    // Poll for a targeted program change from mu-link (scene switching). Returns true and
    // fills program (0-127) + channel (1-16) when the server has sent a NEW one since the
    // last poll; false otherwise. Call from the standalone bridge's poll (message thread);
    // the product then injects MidiMessage::programChange(channel, program) into its MIDI
    // stream → the existing scanMidiProgramChanges → preset hot-swap.
    bool pollProgramChange(int& program, int& channel) noexcept
    {
        if (! attached || ! mem.ownsSlot())
            return false;
        const int slot = mem.claimedSlotIndex();
        auto& s = mem.registry().slots[slot];
        const std::uint32_t epoch = s.pcEpoch.load(std::memory_order_acquire);
        if (epoch == lastPcEpoch)
            return false;
        lastPcEpoch = epoch;
        program = (int) s.pcProgram.load(std::memory_order_relaxed);
        channel = (int) s.pcChannel.load(std::memory_order_relaxed);
        return true;
    }

private:
    static constexpr int kRenderChunk = 512;   // frames rendered per fill pass
    // Top up only when at least this many frames are missing, so the product is rendered in useful
    // chunks rather than in slivers of whatever was consumed since the last pass.
    static constexpr int kMinFill = 256;

    // Producer loop: keep the ring filled to the target depth. Each pass renders into per-channel
    // scratch via onRender, interleaves to the ring layout, and writes up to the missing frames;
    // then bumps the heartbeat and briefly waits.
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
            for (;;)
            {
                if (threadShouldExit()) break;

                // The server reaped our slot (we stalled two seconds or more): stop, rather than keep
                // producing into a ring that may now belong to another client.
                if (! mem.ownsSlot())
                {
                    slotLostFlag.store(true, std::memory_order_relaxed);
                    return;
                }

                // The server may ask for a different depth (a Latency choice in mu-link); 0 = keep ours.
                pollRequestedDepth();

                // The depth to hold: the requested target, but at least two server blocks (it
                // reads a block at a time) and never more than the ring holds.
                TransportSnapshot snap = readTransport(mem.transport());
                const int serverBlock = (int) juce::jmin(snap.blockSize, (std::uint32_t) 4096);   // shared memory: don't trust it blindly
                const int depth = juce::jmin(juce::jmax(targetDepth.load(std::memory_order_relaxed), 2 * serverBlock),
                                             kRingCapacityFrames - kRenderChunk);
                const int buffered = ring.readAvailable();
                const int missing = depth - buffered;
                if (missing < kMinFill)
                    break;   // filled to the target depth (to within a sliver — see kMinFill)
                const int n = juce::jmin(juce::jmin(missing, ring.writeAvailable()), kRenderChunk);
                if (n <= 0)
                    break;

                // Consume-time transport: this look-ahead block plays AFTER everything
                // already queued, so project the master position forward by the buffered
                // frames. That keeps a render-ahead client sample-accurately in sync —
                // the block carries the position at which mu-link will actually play it.
                // Project BOTH the sample position and the musical (ppq) position so the
                // slaved sequencer's bar position lands where mu-link will play this block.
                currentLead = buffered;
                snap.samplePos += (std::uint64_t) buffered;
                const double projSr = snap.sampleRate != 0 ? (double) snap.sampleRate : 48000.0;
                snap.ppqPosition += ((double) buffered / projSr) * (snap.tempoBpm / 60.0);

                for (int c = 0; c < rc; ++c)
                    std::fill(chanPtrs[(std::size_t) c], chanPtrs[(std::size_t) c] + n, 0.0f);

                if (renderCb)
                    renderCb(chanPtrs.data(), rc, n, snap);

                for (int i = 0; i < n; ++i)
                    for (int c = 0; c < rc; ++c)
                        interleaved[(std::size_t) i * (std::size_t) rc + (std::size_t) c] = chanPtrs[(std::size_t) c][i];

                // A render can itself stall long enough for the slot to be reaped and re-claimed, so check
                // again right before the write: two producers on one single-producer ring corrupt it. (What
                // remains is the few instructions between this check and the copy; the server only reaps
                // a heartbeat frozen for two seconds, and the heartbeat is bumped every fill pass.)
                if (! mem.ownsSlot())
                {
                    slotLostFlag.store(true, std::memory_order_relaxed);
                    return;
                }
                const int wrote = ring.writeFrames(interleaved.data(), n);
                if (wrote < n)
                    break;   // ring full for now
            }
            mem.bumpHeartbeat();
            wait(2);          // ~2 ms between fills (woken early by notify() on detach)
        }
    }

    // Apply a changed depth request from the server (producer thread; lock-free).
    void pollRequestedDepth() noexcept
    {
        const int slot = mem.claimedSlotIndex();
        if (slot < 0) return;
        const std::uint32_t req = mem.registry().slots[slot].requestedDepthFrames.load(std::memory_order_relaxed);
        if (req == lastDepthRequest)
            return;
        if (req != 0)
        {
            if (lastDepthRequest == 0) defaultDepth = getTargetDepthFrames();   // remember our own choice to go back to
            setTargetDepthFrames((int) juce::jmin(req, (std::uint32_t) kRingCapacityFrames));
        }
        else if (defaultDepth != 0)
            setTargetDepthFrames(defaultDepth);   // the request was withdrawn: back to the client's own depth
        lastDepthRequest = req;
    }

    MuLinkClientMemory mem;
    RenderCallback     renderCb;
    int                currentLead  = 0;   // producer thread: frames queued ahead of the block in render
    std::atomic<int>   targetDepth  { kDefaultTargetDepthFrames };   // see setTargetDepthFrames
    bool               attached     = false;
    std::atomic<bool>  slotLostFlag { false };   // set by the producer thread when it finds its slot reaped
    int                ringChannels = kMaxChannels;
    std::uint32_t      lastPcEpoch  = 0;   // last scene PC epoch seen (pollProgramChange)
    std::uint32_t      lastDepthRequest = 0;   // last depth request seen (producer thread)
    int                defaultDepth     = 0;   // the depth to return to when a request is withdrawn (0 = none yet)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MuLinkClient)
};

#endif // _WIN32

} // namespace mu_link
