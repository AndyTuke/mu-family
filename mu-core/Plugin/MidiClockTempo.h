#pragma once

// MidiClockTempo — the family's one MIDI-clock tempo estimator. Shared by the plugins'
// MidiClockSync (timestamps from the audio sample clock) and mu-link's MidiClockEstimator
// (timestamps from the MIDI thread), so every μ app follows an external clock the same way.
//
// Each 0xF8 pulse (24 per quarter-note) gives an instantaneous tempo from its interval; a
// one-pole smoothing filter (a simple tempo PLL) tracks the average rate and rejects
// per-pulse jitter. Out-of-range intervals (a dropped or doubled pulse) are ignored.
//
// Pure logic, no JUCE and no atomics: the owner decides which thread calls it.
namespace mu_core
{

class MidiClockTempo
{
public:
    static constexpr double kMinBpm = 20.0;
    static constexpr double kMaxBpm = 400.0;

    // One pulse at `timestampSeconds` (monotonic). Returns true when the estimate moved.
    bool onPulse(double timestampSeconds) noexcept
    {
        bool updated = false;
        if (haveLast)
        {
            const double interval = timestampSeconds - lastTimestamp;
            if (interval > 1.0e-5)
            {
                const double inst = 60.0 / (24.0 * interval);   // 24 ppqn → BPM
                if (inst >= kMinBpm && inst <= kMaxBpm)
                {
                    estBpm  = (estBpm <= 0.0) ? inst                            // seed on first valid
                                              : estBpm + kAlpha * (inst - estBpm);   // smooth
                    updated = true;
                }
            }
        }
        lastTimestamp = timestampSeconds;
        haveLast      = true;
        return updated;
    }

    // Forget the previous pulse (Start / Continue): the next pulse only re-seeds the interval.
    void restartInterval() noexcept { haveLast = false; }

    // Forget the tempo as well (sync switched off and on again): the next two pulses re-seed it.
    void reset() noexcept { haveLast = false; estBpm = 0.0; }

    // Smoothed tempo, or 0 before the first valid interval.
    double bpm() const noexcept { return estBpm; }

private:
    static constexpr double kAlpha = 0.1;   // tempo-PLL smoothing (jitter rejection)

    double lastTimestamp = 0.0;
    double estBpm        = 0.0;
    bool   haveLast      = false;
};

} // namespace mu_core
