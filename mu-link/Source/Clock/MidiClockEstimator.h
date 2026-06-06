#pragma once

#include <atomic>
#include <cstdint>

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
        if (haveLast)
        {
            const double interval = timestampSeconds - lastTimestamp;
            if (interval > 1.0e-5)
            {
                const double inst = 60.0 / (24.0 * interval);   // 24 ppqn → BPM
                if (inst >= kMinBpm && inst <= kMaxBpm)
                {
                    estBpm = (estBpm <= 0.0) ? inst                      // seed on first valid
                                             : estBpm + kAlpha * (inst - estBpm);   // smooth
                    bpmOut.store(estBpm, std::memory_order_relaxed);
                }
            }
        }
        lastTimestamp = timestampSeconds;
        haveLast      = true;
    }

    void onStart() noexcept     // 0xFA — play from the top
    {
        running.store(true,  std::memory_order_relaxed);
        resetRequest.store(true, std::memory_order_release);
        haveLast = false;       // next pulse re-seeds the interval
    }
    void onContinue() noexcept  // 0xFB — resume without resetting position
    {
        running.store(true, std::memory_order_relaxed);
        haveLast = false;
    }
    void onStop() noexcept      // 0xFC
    {
        running.store(false, std::memory_order_relaxed);
    }

    // ─── Audio-thread reads ──────────────────────────────────────────────────────
    double bpm()       const noexcept { return bpmOut.load(std::memory_order_relaxed); }
    bool   isRunning() const noexcept { return running.load(std::memory_order_relaxed); }

    // True exactly once after each Start, so the consumer can rewind the transport to 0.
    bool consumeReset() noexcept { return resetRequest.exchange(false, std::memory_order_acq_rel); }

private:
    static constexpr double kAlpha  = 0.1;     // tempo-PLL smoothing (jitter rejection)
    static constexpr double kMinBpm = 20.0;
    static constexpr double kMaxBpm = 400.0;

    // MIDI-thread state.
    double lastTimestamp = 0.0;
    double estBpm        = 0.0;
    bool   haveLast      = false;

    // Cross-thread outputs.
    std::atomic<double> bpmOut       { 0.0 };
    std::atomic<bool>   running      { false };
    std::atomic<bool>   resetRequest { false };
};

} // namespace mu_link
