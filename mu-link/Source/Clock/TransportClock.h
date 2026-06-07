#pragma once

#include <cstdint>
#include <cmath>

// Sample-accurate master transport clock (server-side, audio-thread).
//
// The clock is driven by the hardware audio callback: each block the server calls
// advance(numFrames). Because position is counted in audio frames at the device's own
// sample rate, the hardware word clock IS the timebase — there is no second clock to
// drift against. Beat/bar positions and MIDI-clock pulses are derived from the same
// frame counter, so everything stays locked to ±0 drift.
namespace mu_link
{

class TransportClock
{
public:
    void prepare(double sampleRateHz, double tempoBpm) noexcept
    {
        sampleRate = sampleRateHz > 0.0 ? sampleRateHz : 48000.0;
        bpm        = tempoBpm > 0.0 ? tempoBpm : 120.0;
        samplePos  = 0;
        beatPos    = 0.0;
    }

    void setTempo(double tempoBpm) noexcept { if (tempoBpm > 0.0) bpm = tempoBpm; }
    void setPlaying(bool shouldPlay) noexcept { playing = shouldPlay; }
    void rewind() noexcept { samplePos = 0; beatPos = 0.0; }

    // Advance the master position by one audio block. No-op while stopped, so a paused
    // transport holds its exact sample position. Beats are ACCUMULATED at the tempo in
    // effect for this block — never recomputed from absolute samples × current tempo — so
    // a tempo change (constant under Internal master, continuous under external-MIDI slave)
    // only changes the rate going forward and never retroactively shifts the beat position.
    void advance(int numFrames) noexcept
    {
        if (playing && numFrames > 0)
        {
            samplePos += (std::uint64_t) numFrames;
            beatPos   += ((double) numFrames / sampleRate) * (bpm / 60.0);
        }
    }

    std::uint64_t samplePosition() const noexcept { return samplePos; }
    double        tempo()          const noexcept { return bpm; }
    bool          isPlaying()      const noexcept { return playing; }

    // Musical position (quarter notes since transport start), accumulated incrementally.
    double beats() const noexcept { return beatPos; }

    // Phase within a bar of `beatsPerBar` beats, in [0, 1).
    double barPhase(double beatsPerBar = 4.0) const noexcept
    {
        if (beatsPerBar <= 0.0) return 0.0;
        const double b = beats();
        return (b - std::floor(b / beatsPerBar) * beatsPerBar) / beatsPerBar;
    }

    // Total MIDI-clock pulses elapsed at `ppqn` (24 for standard MIDI clock). The
    // MIDI-clock-out bridge tracks the delta across blocks to know how many F8 bytes to
    // emit, and at which sample offsets within the block.
    std::uint64_t pulsesElapsed(int ppqn = 24) const noexcept
    {
        return (std::uint64_t) std::floor(beats() * (double) ppqn);
    }

private:
    double        sampleRate = 48000.0;
    double        bpm        = 120.0;
    bool          playing    = false;
    std::uint64_t samplePos  = 0;
    double        beatPos    = 0.0;   // accumulated quarter notes (see advance())
};

} // namespace mu_link
