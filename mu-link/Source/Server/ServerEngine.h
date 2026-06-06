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
    std::uint64_t midiPulsesInBlock = 0;   // 24-ppqn pulses crossed during this block
};

class ServerEngine
{
public:
    // Bind the shared memory the server published into (server-owned). Must outlive the
    // engine. Pass nullptr to run the clock/summing with no IPC (degenerate; tests only).
    void attachMemory(MuLinkServerMemory* sharedMem) noexcept { mem = sharedMem; }

    // Message-thread setup before streaming. Sizes the de-interleave scratch to the
    // device block + max channels; primes the clock at the device sample rate.
    void prepare(double sampleRate, int maxBlockSize, double tempoBpm)
    {
        sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        clock.prepare(sr, tempoBpm);
        scratch.assign((std::size_t) kMaxChannels * (std::size_t) std::max(1, maxBlockSize), 0.0f);
        for (auto& p : clientPeakLevel) p.store(0.0f, std::memory_order_relaxed);
        masterPeakLevel.store(0.0f, std::memory_order_relaxed);
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
            for (int slot = 0; slot < kMaxClients; ++slot)
            {
                if (reg.slots[slot].active.load(std::memory_order_acquire) == 0)
                {
                    clientPeakLevel[slot].store(0.0f, std::memory_order_relaxed);   // freed slot meter falls
                    continue;
                }
                ++stats.activeClients;

                AudioRingView ring = mem->ring(slot);
                const int rc  = ring.numChannels();
                float*    tmp = scratch.data();                       // interleaved, rc channels
                const int got = ring.readFrames(tmp, numFrames);      // ≤ numFrames; short = underrun
                if (got < numFrames)
                    stats.underrunFrames += (numFrames - got);

                // De-interleave the frames we got and add into the master, tracking this
                // client's block peak. A mono ring feeds every output channel; otherwise
                // map channel-for-channel (clamped).
                float clientPk = 0.0f;
                for (int i = 0; i < got; ++i)
                    for (int ch = 0; ch < chans; ++ch)
                    {
                        const int   rch = ch < rc ? ch : rc - 1;
                        const float v   = tmp[(std::size_t) i * (std::size_t) rc + (std::size_t) rch];
                        output[ch][i] += v;
                        clientPk = std::max(clientPk, std::fabs(v));
                    }
                clientPeakLevel[slot].store(clientPk, std::memory_order_relaxed);
            }
        }

        // Master block peak across the summed output, for the master meter.
        float masterPk = 0.0f;
        for (int ch = 0; ch < chans; ++ch)
            for (int i = 0; i < numFrames; ++i)
                masterPk = std::max(masterPk, std::fabs(output[ch][i]));
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
};

} // namespace mu_link
