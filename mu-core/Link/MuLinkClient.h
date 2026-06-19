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

    // Publish the client's current preset name into its registry slot (display-only; mu-link's
    // UI shows it). No-op when detached. Call from the message thread on each preset load.
    void setPresetName(const juce::String& name) noexcept
    {
        if (! attached) return;
        const int slot = mem.claimedSlotIndex();
        if (slot < 0) return;
        auto& dst = mem.registry().slots[slot].presetName;
        const auto utf8 = name.toRawUTF8();
        const int n = juce::jmin((int) kMaxNameChars - 1, (int) std::strlen(utf8));
        std::memcpy(dst, utf8, (size_t) n);
        dst[n] = '\0';
    }

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
        if (! attached)
            return false;
        const int slot = mem.claimedSlotIndex();
        if (slot < 0)
            return false;
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
                // Project BOTH the sample position and the musical (ppq) position so the
                // slaved sequencer's bar position lands where mu-link will play this block.
                TransportSnapshot snap = readTransport(mem.transport());
                const std::uint64_t buffered = (std::uint64_t) ring.readAvailable();
                snap.samplePos += buffered;
                const double projSr = snap.sampleRate != 0 ? (double) snap.sampleRate : 48000.0;
                snap.ppqPosition += ((double) buffered / projSr) * (snap.tempoBpm / 60.0);

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
    std::uint32_t      lastPcEpoch  = 0;   // last scene PC epoch seen (pollProgramChange)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MuLinkClient)
};

#endif // _WIN32

} // namespace mu_link
