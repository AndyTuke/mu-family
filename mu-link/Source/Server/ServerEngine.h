#pragma once

#include "Link/MuLinkProtocol.h"
#include "Link/AudioRing.h"
#include "../Ipc/MuLinkServerMemory.h"
#include "../Clock/TransportClock.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

// ServerEngine — the mu-link real-time core, independent of any audio backend so it can
// be unit-tested headless (juce_core only). The device-owning AudioServer feeds it the
// hardware callback's output buffer; everything sacred — the master clock, the transport
// publish, the lock-free summing, the MIDI-clock pulse math — lives here.
//
// Per block (design §3): publish the start-of-block TransportBlock (clients render the
// next block ahead against it), sum every active client's ring into the master output
// (a short ring read is an underrun → that client contributes silence for the missing
// tail, never a click on the master), then advance the clock by the block length.
namespace mu_link
{

struct BlockStats
{
    int           activeClients     = 0;
    int           underrunFrames    = 0;   // summed missing frames across clients this block
    int           reapedClients     = 0;   // slots reaped this block (died without detaching)
    std::uint64_t midiPulsesInBlock = 0;   // 24-ppqn pulses crossed during this block
};

class ServerEngine
{
public:
    ServerEngine()
    {
        // Mixer defaults (set once; survive device/SR changes via prepare()).
        for (int i = 0; i < kMaxClients; ++i)
        {
            clientGain[i].store(1.0f, std::memory_order_relaxed);
            clientPan [i].store(0.0f, std::memory_order_relaxed);
            clientMute[i].store(0,    std::memory_order_relaxed);
            clientSolo[i].store(0,    std::memory_order_relaxed);
        }
    }

    // Bind the shared memory the server published into (server-owned). Must outlive the
    // engine. Pass nullptr to run the clock/summing with no IPC (degenerate; tests only).
    void attachMemory(MuLinkServerMemory* sharedMem) noexcept { mem = sharedMem; }

    // ─── Per-client mixer (message thread sets, audio thread reads) ──────────────
    void setClientGain(int slot, float linearGain) noexcept { clientGain[slot].store(linearGain, std::memory_order_relaxed); }
    void setClientPan (int slot, float pan)         noexcept { clientPan[slot].store(std::clamp(pan, -1.0f, 1.0f), std::memory_order_relaxed); }
    void setClientMute(int slot, bool muted)        noexcept { clientMute[slot].store(muted ? 1u : 0u, std::memory_order_relaxed); }
    void setClientSolo(int slot, bool soloed)       noexcept { clientSolo[slot].store(soloed ? 1u : 0u, std::memory_order_relaxed); }
    void setMasterGain(float linearGain)            noexcept { masterGainLevel.store(linearGain, std::memory_order_relaxed); }

    float clientGainValue(int slot) const noexcept { return clientGain[slot].load(std::memory_order_relaxed); }
    float clientPanValue (int slot) const noexcept { return clientPan[slot].load(std::memory_order_relaxed); }
    bool  clientMuted    (int slot) const noexcept { return clientMute[slot].load(std::memory_order_relaxed) != 0; }
    bool  clientSoloed   (int slot) const noexcept { return clientSolo[slot].load(std::memory_order_relaxed) != 0; }
    float masterGainValue()         const noexcept { return masterGainLevel.load(std::memory_order_relaxed); }

    // Message-thread setup before streaming. Sizes the de-interleave scratch to the
    // device block + max channels; primes the clock at the device sample rate.
    void prepare(double sampleRate, int maxBlockSize, double tempoBpm)
    {
        sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        clock.prepare(sr, tempoBpm);
        scratch.assign((std::size_t) kMaxChannels * (std::size_t) std::max(1, maxBlockSize), 0.0f);
        for (auto& p : clientPeakLevel) p.store(0.0f, std::memory_order_relaxed);
        masterPeakLevel.store(0.0f, std::memory_order_relaxed);

        // A live client bumps its heartbeat every render-loop pass (sub-millisecond); if a
        // slot's heartbeat is frozen for this many frames it has died without detaching and
        // is reaped. 2 s is far longer than any scheduling hiccup, so no false positives.
        reapThresholdFrames = (std::uint64_t) (2.0 * sr);
        for (int i = 0; i < kMaxClients; ++i) { lastHeartbeat[i] = 0; staleFrames[i] = 0; }
    }

    void setTempo(double bpm) noexcept   { clock.setTempo(bpm); }
    void setPlaying(bool playing) noexcept { clock.setPlaying(playing); }
    void rewind() noexcept               { clock.rewind(); }

    const TransportClock& transportClock() const noexcept { return clock; }

    // Latest per-block peak (linear, 0–1) for metering. Per active client and the summed
    // master; the GUI reads these on the message thread and applies its own ballistics.
    float clientPeak(int slot) const noexcept { return clientPeakLevel[slot].load(std::memory_order_relaxed); }
    float masterPeak()         const noexcept { return masterPeakLevel.load(std::memory_order_relaxed); }

    // Render one block into `output` (per-channel device buffers, numChannels ≤ kMaxChannels).
    // Audio-thread, lock-free, no allocation. Returns per-block stats for logging/metering.
    BlockStats renderBlock(float* const* output, int numChannels, int numFrames) noexcept
    {
        const int chans = std::min(numChannels, kMaxChannels);

        // Clear the master, then sum each active client into it.
        for (int ch = 0; ch < numChannels; ++ch)
            std::fill(output[ch], output[ch] + numFrames, 0.0f);

        BlockStats stats;

        // Publish the start-of-block transport so clients align the block they render ahead.
        if (mem != nullptr)
            writeTransport(mem->transport(), snapshot((std::uint32_t) numFrames));

        if (mem != nullptr)
        {
            auto& reg = mem->registry();

            // Solo: if any active client is soloed, only soloed clients are audible.
            bool anySolo = false;
            for (int s = 0; s < kMaxClients; ++s)
                if (reg.slots[s].active.load(std::memory_order_acquire) != 0
                    && clientSolo[s].load(std::memory_order_relaxed) != 0)
                { anySolo = true; break; }

            for (int slot = 0; slot < kMaxClients; ++slot)
            {
                if (reg.slots[slot].active.load(std::memory_order_acquire) == 0)
                {
                    clientPeakLevel[slot].store(0.0f, std::memory_order_relaxed);   // freed slot meter falls
                    lastHeartbeat[slot] = 0;
                    staleFrames[slot]   = 0;
                    continue;
                }

                // Liveness: reap a slot whose heartbeat has frozen (client crashed without
                // detaching) so its zombie ring/meter doesn't linger and its slot frees up.
                const std::uint64_t hb = reg.slots[slot].heartbeat.load(std::memory_order_relaxed);
                if (hb != lastHeartbeat[slot])
                {
                    lastHeartbeat[slot] = hb;
                    staleFrames[slot]   = 0;
                }
                else
                {
                    staleFrames[slot] += (std::uint64_t) numFrames;
                    if (staleFrames[slot] > reapThresholdFrames)
                    {
                        reg.slots[slot].active.store(0, std::memory_order_release);   // reap
                        clientPeakLevel[slot].store(0.0f, std::memory_order_relaxed);
                        lastHeartbeat[slot] = 0;
                        staleFrames[slot]   = 0;
                        ++stats.reapedClients;
                        continue;                                                    // don't sum a dead client
                    }
                }
                ++stats.activeClients;

                AudioRingView ring = mem->ring(slot);
                const int rc  = ring.numChannels();
                float*    tmp = scratch.data();                       // interleaved, rc channels
                const int got = ring.readFrames(tmp, numFrames);      // ≤ numFrames; short = underrun
                if (got < numFrames)
                    stats.underrunFrames += (numFrames - got);

                // Mixer strip: mute / solo gate, then gain + linear stereo balance.
                const bool  muted   = clientMute[slot].load(std::memory_order_relaxed) != 0;
                const bool  soloed  = clientSolo[slot].load(std::memory_order_relaxed) != 0;
                const bool  audible = ! muted && (! anySolo || soloed);
                const float gain    = audible ? clientGain[slot].load(std::memory_order_relaxed) : 0.0f;
                const float pan     = clientPan[slot].load(std::memory_order_relaxed);
                const float gainL   = gain * (pan > 0.0f ? 1.0f - pan : 1.0f);
                const float gainR   = gain * (pan < 0.0f ? 1.0f + pan : 1.0f);

                // De-interleave the frames we got, apply the strip, and add into the master,
                // tracking this client's post-fader peak. A mono ring feeds every output
                // channel; otherwise map channel-for-channel (clamped). ch1 = R, else L.
                float clientPk = 0.0f;
                for (int i = 0; i < got; ++i)
                    for (int ch = 0; ch < chans; ++ch)
                    {
                        const int   rch = ch < rc ? ch : rc - 1;
                        const float g   = (ch == 1) ? gainR : gainL;
                        const float v   = tmp[(std::size_t) i * (std::size_t) rc + (std::size_t) rch] * g;
                        output[ch][i] += v;
                        clientPk = std::max(clientPk, std::fabs(v));
                    }
                clientPeakLevel[slot].store(clientPk, std::memory_order_relaxed);
            }
        }

        // Apply the master gain, then take the block peak for the master meter.
        const float masterGain = masterGainLevel.load(std::memory_order_relaxed);
        float masterPk = 0.0f;
        for (int ch = 0; ch < chans; ++ch)
            for (int i = 0; i < numFrames; ++i)
            {
                output[ch][i] *= masterGain;
                masterPk = std::max(masterPk, std::fabs(output[ch][i]));
            }
        masterPeakLevel.store(masterPk, std::memory_order_relaxed);

        // Advance the master by this block and report how many MIDI-clock pulses it crossed.
        const std::uint64_t before = clock.pulsesElapsed(24);
        clock.advance(numFrames);
        stats.midiPulsesInBlock = clock.pulsesElapsed(24) - before;
        return stats;
    }

private:
    TransportSnapshot snapshot(std::uint32_t numFrames) const noexcept
    {
        TransportSnapshot s;
        s.samplePos  = clock.samplePosition();
        s.tempoBpm   = clock.tempo();
        s.sampleRate = (std::uint32_t) sr;
        s.playing    = clock.isPlaying() ? 1u : 0u;
        s.blockSize  = numFrames;
        return s;
    }

    MuLinkServerMemory* mem = nullptr;
    TransportClock      clock;
    double              sr = 48000.0;
    std::vector<float>  scratch;   // de-interleave buffer, allocated in prepare()
    std::atomic<float>  clientPeakLevel[kMaxClients];   // latest per-block peak, per slot
    std::atomic<float>  masterPeakLevel { 0.0f };       // latest per-block summed peak

    // Liveness tracking (audio-thread only). A frozen heartbeat for reapThresholdFrames
    // means the client died without detaching → reap its slot.
    std::uint64_t lastHeartbeat[kMaxClients] {};
    std::uint64_t staleFrames[kMaxClients] {};
    std::uint64_t reapThresholdFrames = 0;

    // Per-client mixer strip + master (message thread writes, audio thread reads).
    std::atomic<float>         clientGain[kMaxClients];
    std::atomic<float>         clientPan[kMaxClients];
    std::atomic<std::uint32_t> clientMute[kMaxClients];
    std::atomic<std::uint32_t> clientSolo[kMaxClients];
    std::atomic<float>         masterGainLevel { 1.0f };
};

} // namespace mu_link
