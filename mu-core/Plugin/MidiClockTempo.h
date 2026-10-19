#pragma once

// MidiClockTempo — the family's one MIDI-clock tempo estimator. Shared by the plugins'
// MidiClockSync (timestamps from the audio sample clock) and mu-link's MidiClockEstimator
// (timestamps from the MIDI thread), so every μ app follows an external clock the same way.
//
// Each 0xF8 pulse (24 per quarter-note) is timestamped, and the tempo is the average rate over the
// last four beats of pulses: the span from the oldest pulse in the window to the newest, divided into the
// pulse count. A timestamp is only as exact as its audio block (pulses in one block bunch together),
// and that error does not grow with the window, so averaging four beats settles the reading to a
// fraction of a BPM where a pulse-to-pulse filter jitters. A gap too long to be a pulse (a dropout)
// restarts the window. The cost is that a real tempo change is followed over about four beats.
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
        if (count > 0)
        {
            const double interval = timestampSeconds - stamp[(head + kWindow) % (kWindow + 1)];   // newest so far
            if (interval < 0.0 || interval > 60.0 / (24.0 * kMinBpm)) count = 0;   // dropout or time jump
        }

        // Push into the ring: the newest sits at head-1, the oldest at head when full.
        stamp[head] = timestampSeconds;
        head  = (head + 1) % (kWindow + 1);
        count = count < kWindow + 1 ? count + 1 : count;
        if (count < 2) return false;

        const double oldest = stamp[(head + (kWindow + 1) - count) % (kWindow + 1)];
        const double span   = timestampSeconds - oldest;
        if (span <= 1.0e-5) return false;
        const double bpm = 60.0 * (count - 1) / (24.0 * span);   // 24 ppqn
        if (bpm < kMinBpm || bpm > kMaxBpm) return false;
        estBpm = bpm;
        return true;
    }

    // Forget the previous pulses (Start / Continue): the next pulses re-seed the window.
    void restartInterval() noexcept { count = 0; }

    // Forget the tempo as well (sync switched off and on again): the next two pulses re-seed it.
    void reset() noexcept { count = 0; estBpm = 0.0; }

    // Averaged tempo, or 0 before the first valid interval.
    double bpm() const noexcept { return estBpm; }

private:
    static constexpr int kWindow = 96;   // four beats of pulses

    double stamp[kWindow + 1] {};
    int    head   = 0;
    int    count  = 0;
    double estBpm = 0.0;
};

} // namespace mu_core
