#pragma once

#include "Link/MuLinkProtocol.h"
#include "Link/AudioRing.h"
#include "../Ipc/MuLinkServerMemory.h"
#include "../Clock/TransportClock.h"
#include "../Clock/MidiClockEstimator.h"
#include "Audio/FX/Insert/EqInsert.h"   // mu-core: shared 3-band EQ insert (per-client strip)
#include "Audio/InsertProcessor.h"      // mu-core: full insert-FX rack (two master-bus inserts)

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

// The master clock's tempo + transport source. Internal = mu-link is master (user tempo +
// play button); ExternalMidi = mu-link slaves to incoming MIDI clock (L7).
enum class ClockSource { Internal, ExternalMidi };

struct BlockStats
{
    int           activeClients     = 0;
    int           underrunFrames    = 0;   // summed missing frames across clients this block
    int           reapedClients     = 0;   // slots reaped this block (died without detaching)
    std::uint64_t midiPulsesInBlock = 0;   // 24-ppqn pulses crossed during this block
};

// Master safety soft-clip. Bit-exact below the knee (~ -0.9 dBFS) and C1-continuous into a
// tanh knee that asymptotes to ±1.0, so the summed bus can NEVER hard-clip the device no
// matter how hot a client — or the sum of several — gets. The knee sits just below full
// scale so normal-level program material (a single instrument near 0 dBFS) passes through
// untouched; it only shapes genuine overs (e.g. several hot clients summed). A 0.8 knee
// (-1.9 dBFS) was low enough to colour even one moderately-hot instrument → subtle harmonic
// distortion; 0.9 keeps single-source playback transparent while still guaranteeing the cap.
inline float masterSoftClip(float x) noexcept
{
    constexpr float t = 0.9f;                       // knee start; below this is unchanged
    const float a = std::fabs(x);
    if (a <= t) return x;
    const float shaped = t + (1.0f - t) * std::tanh((a - t) / (1.0f - t));
    return x < 0.0f ? -shaped : shaped;
}

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
            for (int b = 0; b < kEqBands; ++b)
                clientEqParam[i][b].store(0.5f, std::memory_order_relaxed);   // 0.5 = flat (0 dB)
            clientEqActive[i].store(0u, std::memory_order_relaxed);           // flat → EQ skipped
        }
        eqVp.insertAlgo = 6;   // mu-core insert algo 6 = 3-Band EQ
    }

    // Bind the shared memory the server published into (server-owned). Must outlive the
    // engine. Pass nullptr to run the clock/summing with no IPC (degenerate; tests only).
    void attachMemory(MuLinkServerMemory* sharedMem) noexcept { mem = sharedMem; }

    // ─── Per-client mixer (message thread sets, audio thread reads) ──────────────
    // All slot-indexed accessors guard the index — an out-of-range slot is a no-op (read
    // returns a neutral default) rather than an out-of-bounds atomic-array access.
    static bool validSlot(int slot) noexcept { return slot >= 0 && slot < kMaxClients; }

    void setClientGain(int slot, float linearGain) noexcept { if (validSlot(slot)) clientGain[slot].store(linearGain, std::memory_order_relaxed); }
    void setClientPan (int slot, float pan)         noexcept { if (validSlot(slot)) clientPan[slot].store(std::clamp(pan, -1.0f, 1.0f), std::memory_order_relaxed); }
    void setClientMute(int slot, bool muted)        noexcept { if (validSlot(slot)) clientMute[slot].store(muted ? 1u : 0u, std::memory_order_relaxed); }
    void setClientSolo(int slot, bool soloed)       noexcept { if (validSlot(slot)) clientSolo[slot].store(soloed ? 1u : 0u, std::memory_order_relaxed); }
    void setMasterGain(float linearGain)            noexcept { masterGainLevel.store(linearGain, std::memory_order_relaxed); }

    float clientGainValue(int slot) const noexcept { return validSlot(slot) ? clientGain[slot].load(std::memory_order_relaxed) : 1.0f; }
    float clientPanValue (int slot) const noexcept { return validSlot(slot) ? clientPan[slot].load(std::memory_order_relaxed) : 0.0f; }
    bool  clientMuted    (int slot) const noexcept { return validSlot(slot) && clientMute[slot].load(std::memory_order_relaxed) != 0; }
    bool  clientSoloed   (int slot) const noexcept { return validSlot(slot) && clientSolo[slot].load(std::memory_order_relaxed) != 0; }
    float masterGainValue()         const noexcept { return masterGainLevel.load(std::memory_order_relaxed); }

    // Per-client 3-band EQ insert. `band` 0..3 = Low / Mid / Mid-Hz / High; `norm01` is the
    // normalised knob value (0.5 = flat / 0 dB). A strip whose Low/Mid/High are all centred
    // is flat → the audio thread skips the EQ entirely (bit-exact passthrough, zero CPU);
    // Mid-Hz alone never changes flatness, so it doesn't arm the EQ.
    static constexpr int kEqBands = 4;
    void setClientEqParam(int slot, int band, float norm01) noexcept
    {
        if (! validSlot(slot) || band < 0 || band >= kEqBands) return;
        clientEqParam[slot][band].store(std::clamp(norm01, 0.0f, 1.0f), std::memory_order_relaxed);
        auto centred = [this, slot](int b)
        { return std::fabs(clientEqParam[slot][b].load(std::memory_order_relaxed) - 0.5f) < 1.0e-4f; };
        const bool flat = centred(0) && centred(1) && centred(3);
        clientEqActive[slot].store(flat ? 0u : 1u, std::memory_order_relaxed);
    }
    float clientEqValue(int slot, int band) const noexcept
    {
        return (validSlot(slot) && band >= 0 && band < kEqBands)
             ? clientEqParam[slot][band].load(std::memory_order_relaxed) : 0.5f;
    }

    // Two master-bus insert effects (like mu-clid / mu-tant's master inserts). `which` 0/1.
    // `algo` is the mu-core insert algorithm index (0 = None → bypassed). Params are the four
    // normalised 0..1 insert slots (de-normalised per-algo inside InsertProcessor).
    static constexpr int kMasterInserts = 2;
    void setMasterInsertAlgo(int which, int algo) noexcept
    {
        if (which < 0 || which >= kMasterInserts) return;
        masterInsAlgo[which].store(juce::jlimit(0, InsertProcessor::kNumInsertAlgos - 1, algo), std::memory_order_relaxed);
    }
    void setMasterInsertParam(int which, int slot, float norm01) noexcept
    {
        if (which < 0 || which >= kMasterInserts || slot < 0 || slot >= VoiceParams::kInsertSlotCount) return;
        masterInsParam[which][slot].store(std::clamp(norm01, 0.0f, 1.0f), std::memory_order_relaxed);
    }
    int   masterInsertAlgo(int which) const noexcept
    { return (which >= 0 && which < kMasterInserts) ? masterInsAlgo[which].load(std::memory_order_relaxed) : 0; }
    float masterInsertParam(int which, int slot) const noexcept
    {
        return (which >= 0 && which < kMasterInserts && slot >= 0 && slot < VoiceParams::kInsertSlotCount)
             ? masterInsParam[which][slot].load(std::memory_order_relaxed) : 0.0f;
    }

    // Message-thread setup before streaming. Sizes the de-interleave scratch to the
    // device block + max channels; primes the clock at the device sample rate.
    void prepare(double sampleRate, int maxBlockSize, double tempoBpm)
    {
        sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        clock.prepare(sr, tempoBpm);
        maxBlockFrames = std::max(1, maxBlockSize);
        scratch.assign((std::size_t) kMaxChannels * (std::size_t) maxBlockFrames, 0.0f);
        // Per-client EQ DSP + de-interleave scratch (only used when a strip's EQ is armed).
        for (int i = 0; i < kMaxClients; ++i)
            clientEqInsert[i].prepare(sr, maxBlockFrames);
        eqScratch.setSize(kMaxChannels, maxBlockFrames, false, false, true);
        eqScratch.clear();
        for (auto& mi : masterInsert) mi.prepare(sr, maxBlockFrames);
        for (auto& p : clientPeakLevel) p.store(0.0f, std::memory_order_relaxed);
        masterPeakLevel.store(0.0f, std::memory_order_relaxed);

        // A live client bumps its heartbeat every render-loop pass (sub-millisecond); if a
        // slot's heartbeat is frozen for this many frames it has died without detaching and
        // is reaped. 2 s is far longer than any scheduling hiccup, so no false positives.
        reapThresholdFrames = (std::uint64_t) (2.0 * sr);

        // External-MIDI stall: if no new clock pulse arrives for this many frames the source
        // has gone silent without a Stop (e.g. cable pulled) — treat the clock as lost. 0.5 s
        // is > 4 pulse periods even at the 20 BPM floor, so a live clock never trips it.
        extStaleThresholdFrames = (std::uint64_t) (0.5 * sr);
        lastExtPulseCount = 0; extStaleFrames = 0;
        for (int i = 0; i < kMaxClients; ++i) { lastHeartbeat[i] = 0; staleFrames[i] = 0; }
    }

    void setTempo(double bpm) noexcept   { clock.setTempo(bpm); }
    void setPlaying(bool playing) noexcept { clock.setPlaying(playing); }
    void rewind() noexcept               { clock.rewind(); }

    const TransportClock& transportClock() const noexcept { return clock; }

    // Clock source (message thread sets, audio thread reads). In ExternalMidi mode the
    // engine follows the attached MidiClockEstimator each block instead of the user tempo.
    void        setClockSource(ClockSource s) noexcept { clockSource.store(s, std::memory_order_relaxed); }
    ClockSource clockSourceMode()       const noexcept { return clockSource.load(std::memory_order_relaxed); }
    void        attachMidiClock(MidiClockEstimator* e) noexcept { midiClock = e; }

    // Latest per-block peak (linear, 0–1) for metering. Per active client and the summed
    // master; the GUI reads these on the message thread and applies its own ballistics.
    float clientPeak(int slot) const noexcept { return validSlot(slot) ? clientPeakLevel[slot].load(std::memory_order_relaxed) : 0.0f; }
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

        // External MIDI clock (slave): follow the smoothed tempo + transport BEFORE we
        // publish/advance, so the master rides the external clock yet stays sample-accurate
        // (the frame counter is still the timebase; only its tempo tracks the estimate).
        if (clockSource.load(std::memory_order_relaxed) == ClockSource::ExternalMidi && midiClock != nullptr)
        {
            if (midiClock->consumeReset())
                clock.rewind();

            // Stall watchdog: track whether the pulse count is still advancing. A source that
            // stopped sending 0xF8 without a 0xFC freezes it → treat the clock as lost (stop)
            // instead of coasting forever at the last tempo.
            const std::uint64_t pc = midiClock->pulseCount();
            if (pc != lastExtPulseCount) { lastExtPulseCount = pc; extStaleFrames = 0; }
            else                         { extStaleFrames += (std::uint64_t) numFrames; }
            const bool extAlive = extStaleFrames <= extStaleThresholdFrames;

            const double extBpm = midiClock->bpm();
            if (extBpm > 0.0 && extAlive)
                clock.setTempo(extBpm);
            clock.setPlaying(midiClock->isRunning() && extAlive);
        }

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
                // Never read more than the de-interleave scratch was sized for in prepare()
                // — a driver delivering a block larger than the prepared max must not overrun.
                const int want = std::min(numFrames, maxBlockFrames);
                const int got  = ring.readFrames(tmp, want);          // ≤ want; short = underrun
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
                // Per-channel constants (source index + gain) are hoisted out of the sample
                // loop — the inner loop is then a tight multiply-add on the audio thread.
                float clientPk = 0.0f;
                if (clientEqActive[slot].load(std::memory_order_relaxed) != 0 && got > 0)
                {
                    // Armed EQ: de-interleave the client's frames (mono ring feeds every
                    // channel), run the 3-band EQ in place, then sum the EQ'd output into
                    // the master with the strip gain/pan + post-fader peak.
                    for (int ch = 0; ch < chans; ++ch)
                    {
                        const int rch  = ch < rc ? ch : rc - 1;
                        float*    dst  = eqScratch.getWritePointer(ch);
                        for (int i = 0; i < got; ++i)
                            dst[i] = tmp[(std::size_t) i * (std::size_t) rc + (std::size_t) rch];
                    }
                    for (int b = 0; b < kEqBands; ++b)
                        eqVp.insertParam[b] = clientEqParam[slot][b].load(std::memory_order_relaxed);
                    float grDummy = 0.0f;
                    clientEqInsert[slot].process(eqScratch, got, chans, eqVp, grDummy);

                    for (int ch = 0; ch < chans; ++ch)
                    {
                        const float  g     = (ch == 1) ? gainR : gainL;
                        const float* src   = eqScratch.getReadPointer(ch);
                        float* const outCh = output[ch];
                        for (int i = 0; i < got; ++i)
                        {
                            const float v = src[i] * g;
                            outCh[i] += v;
                            clientPk = std::max(clientPk, std::fabs(v));
                        }
                    }
                }
                else
                {
                    // Flat strip (default): the original interleaved sum — bit-exact, no EQ cost.
                    for (int ch = 0; ch < chans; ++ch)
                    {
                        const int   rch = ch < rc ? ch : rc - 1;
                        const float g   = (ch == 1) ? gainR : gainL;
                        float* const outCh = output[ch];
                        for (int i = 0; i < got; ++i)
                        {
                            const float v = tmp[(std::size_t) i * (std::size_t) rc + (std::size_t) rch] * g;
                            outCh[i] += v;
                            clientPk = std::max(clientPk, std::fabs(v));
                        }
                    }
                }
                clientPeakLevel[slot].store(clientPk, std::memory_order_relaxed);
            }
        }

        // Master-bus insert effects (two slots, like the products' master inserts). Each runs
        // in place on the summed bus when its algorithm isn't None (0). Wrapping `output`
        // (already-summed device buffers) in an AudioBuffer is alloc-free — it just refers.
        {
            juce::AudioBuffer<float> masterBuf(output, chans, numFrames);
            for (int w = 0; w < kMasterInserts; ++w)
            {
                const int algo = masterInsAlgo[w].load(std::memory_order_relaxed);
                if (algo <= 0) continue;
                masterVp.insertAlgo = algo;
                for (int s = 0; s < VoiceParams::kInsertSlotCount; ++s)
                    masterVp.insertParam[s] = masterInsParam[w][s].load(std::memory_order_relaxed);
                masterInsert[w].process(masterBuf, numFrames, chans, masterVp);
            }
        }

        // Apply the master gain + safety soft-clip, then take the block peak for the meter.
        const float masterGain = masterGainLevel.load(std::memory_order_relaxed);
        float masterPk = 0.0f;
        for (int ch = 0; ch < chans; ++ch)
            for (int i = 0; i < numFrames; ++i)
            {
                const float v = masterSoftClip(output[ch][i] * masterGain);
                output[ch][i] = v;
                masterPk = std::max(masterPk, std::fabs(v));
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
        s.samplePos   = clock.samplePosition();
        s.ppqPosition = clock.beats();              // accumulated; tempo-change safe
        s.tempoBpm    = clock.tempo();
        s.sampleRate  = (std::uint32_t) sr;
        s.playing     = clock.isPlaying() ? 1u : 0u;
        s.blockSize   = numFrames;
        return s;
    }

    MuLinkServerMemory* mem = nullptr;
    TransportClock      clock;
    double              sr = 48000.0;
    int                 maxBlockFrames = 0;   // de-interleave scratch capacity (frames)
    std::vector<float>  scratch;   // de-interleave buffer, allocated in prepare()

    std::atomic<ClockSource> clockSource { ClockSource::Internal };
    MidiClockEstimator*      midiClock = nullptr;   // not owned (lives in AudioServer)

    // External-MIDI stall watchdog (audio-thread only).
    std::uint64_t lastExtPulseCount       = 0;
    std::uint64_t extStaleFrames          = 0;
    std::uint64_t extStaleThresholdFrames = 0;
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

    // Per-client 3-band EQ insert (message thread writes params, audio thread reads/runs).
    std::atomic<float>         clientEqParam[kMaxClients][kEqBands];   // normalised, 0.5 = flat
    std::atomic<std::uint32_t> clientEqActive[kMaxClients];           // 0 = flat (skip), 1 = armed
    EqInsert                   clientEqInsert[kMaxClients];           // audio-thread DSP state
    VoiceParams                eqVp;                                  // shared param carrier (algo 6)
    juce::AudioBuffer<float>   eqScratch;                             // de-interleave buffer (EQ path)

    // Two master-bus inserts (message thread writes params, audio thread reads/runs).
    std::atomic<int>           masterInsAlgo[kMasterInserts] { };                       // 0 = None
    std::atomic<float>         masterInsParam[kMasterInserts][VoiceParams::kInsertSlotCount] { };
    InsertProcessor            masterInsert[kMasterInserts];          // audio-thread DSP state
    VoiceParams                masterVp;                              // shared param carrier
};

} // namespace mu_link
