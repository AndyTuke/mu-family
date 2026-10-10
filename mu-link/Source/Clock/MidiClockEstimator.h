#pragma once

#include <atomic>
#include <cstdint>
#include "Plugin/MidiClockTempo.h"   // mu-core: the shared tempo PLL

// MidiClockEstimator — turns an incoming MIDI clock stream into a smooth tempo + transport
// state that mu-link's sample-accurate master clock can follow (L7, external-clock slave).
//
// The key to staying "rock solid" while syncing to jittery MIDI clock: we do NOT use the
// raw F8 pulses as the timebase. Each F8 (24 per quarter-note) gives an instantaneous tempo
// from its interval; a one-pole smoothing filter (a simple tempo PLL) tracks the *average*
// rate and rejects per-pulse jitter. The audio frame counter remains the timebase — only
// its tempo follows this estimate — so downstream clients + the MIDI-clock OUT stay
// sample-accurate (design §3.1, amended decision #4).
//
// Pure logic (no JUCE): fed monotonic timestamps in seconds by the MIDI adapter, so it is
// unit-testable headless. Pulse handling runs on the MIDI thread; the cross-thread outputs
// (bpm / running / reset) are atomics read by the audio thread.
namespace mu_link
{

class MidiClockEstimator
{
public:
    // One MIDI Clock pulse (0xF8) arrived at `timestampSeconds` (monotonic).
    void onClockPulse(double timestampSeconds) noexcept
    {
        if (tempo.onPulse(timestampSeconds))
            bpmOut.store(tempo.bpm(), std::memory_order_relaxed);
        pulseCounter.fetch_add(1, std::memory_order_relaxed);   // liveness tick (stall detection)
    }

    void onStart() noexcept     // 0xFA — play from the top
    {
        locateRequest.store(false, std::memory_order_relaxed);   // a Start overrides an earlier locate
        resetRequest.store(true, std::memory_order_relaxed);
        running.store(true, std::memory_order_release);   // last: publishes the reset with it
        tempo.restartInterval();   // next pulse re-seeds the interval
    }
    void onContinue() noexcept  // 0xFB — resume without resetting position
    {
        running.store(true, std::memory_order_release);   // publishes a Song Position sent before it
        tempo.restartInterval();
    }
    void onStop() noexcept      // 0xFC
    {
        running.store(false, std::memory_order_relaxed);
    }
    // 0xF2 Song Position Pointer — the master located to sixteenths (MIDI beats). Only valid
    // while stopped (MIDI spec); the next Continue resumes from there.
    void onSongPosition(int sixteenths) noexcept
    {
        if (running.load(std::memory_order_relaxed))
            return;
        resetRequest.store(false, std::memory_order_relaxed);   // a later locate overrides an earlier Start
        locateBeats.store(sixteenths / 4.0, std::memory_order_relaxed);
        locateRequest.store(true, std::memory_order_release);
    }

    // ─── Audio-thread reads ──────────────────────────────────────────────────────
    double bpm()       const noexcept { return bpmOut.load(std::memory_order_relaxed); }
    bool   isRunning() const noexcept { return running.load(std::memory_order_acquire); }

    // Monotonic count of clock pulses received — the audio thread watches this for a STALL
    // (a source that stops sending 0xF8 without a 0xFC, e.g. a pulled cable): if it stops
    // advancing the engine treats the external clock as lost rather than playing forever at
    // the frozen tempo. The frame counter stays the timebase, so detection lives server-side.
    std::uint64_t pulseCount() const noexcept { return pulseCounter.load(std::memory_order_relaxed); }

    // True exactly once after each Start, so the consumer can rewind the transport to 0.
    bool consumeReset() noexcept { return resetRequest.exchange(false, std::memory_order_acq_rel); }

    // True exactly once after each Song Position Pointer, with the located beat (quarter notes).
    bool consumeLocate(double& beats) noexcept
    {
        if (! locateRequest.exchange(false, std::memory_order_acq_rel))
            return false;
        beats = locateBeats.load(std::memory_order_relaxed);
        return true;
    }

private:
    // MIDI-thread state: the shared family tempo estimator.
    mu_core::MidiClockTempo tempo;

    // Cross-thread outputs.
    std::atomic<double>        bpmOut       { 0.0 };
    std::atomic<bool>          running      { false };
    std::atomic<bool>          resetRequest { false };
    std::atomic<bool>          locateRequest { false };
    std::atomic<double>        locateBeats   { 0.0 };
    std::atomic<std::uint64_t> pulseCounter { 0 };   // liveness; see pulseCount()
};

} // namespace mu_link
