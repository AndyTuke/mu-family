#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include "Plugin/MidiClockTempo.h"   // mu-core: the shared tempo PLL

// MIDI clock sync state machine — a SHARED, plugin-agnostic mu-core component
// (lifted from mu-clid so every synth product slaves to external MIDI clock the
// same way). Pure JUCE + atomics, no product symbols.
//
// Audio thread calls process() each block; it scans the MidiBuffer for real-time
// messages (0xF8 clock tick, 0xFA/FB/FC start/continue/stop), feeds each tick to the
// shared MidiClockTempo estimator (the same one mu-link uses), and returns the
// start-of-block beat position.
//
// All cross-thread reads (isEnabled, isPlaying, getBpm, getBeatPosUI) are backed by
// atomics and safe to call from the message thread. The audio-thread-only fields
// (pulses_, sampleClock_, tempo_, startedInBlock_, blockTicks_, blockTransport_) must not be accessed from any other thread.
class MidiClockSync
{
public:
    // ── Message-thread setters ───────────────────────────────────────────
    void setEnabled(bool on)
    {
        enabled_.store(on, std::memory_order_relaxed);
        if (!on)
        {
            isPlaying_.store(false);
            bpmEst_.store(0.0);   // no tempo estimate until pulses arrive again
        }
    }

    void setMessages(int mode)   // 0=clock only, 1=transport only, 2=both
    {
        mode = juce::jlimit(0, 2, mode);
        messages_.store(mode, std::memory_order_relaxed);
        if (mode == 0) isPlaying_.store(false);   // Stop is ignored from now on, so drop the clock's play state
    }

    // ── Cross-thread reads ───────────────────────────────────────────────
    bool   isEnabled()    const { return enabled_.load(std::memory_order_relaxed); }
    int    getMessages()  const { return messages_.load(std::memory_order_relaxed); }
    bool   isPlaying()    const { return isPlaying_.load(); }
    double getBpm()       const { return bpmEst_.load(); }   // 0 = no estimate yet (no pulses seen)
    double getBeatPosUI() const { return beatPosUI_.load(std::memory_order_relaxed); }

    // ── Audio thread ─────────────────────────────────────────────────────
    // Scans midi for real-time messages; updates internal state; returns the
    // start-of-block beat position (0.0 if sync is disabled).
    double process(const juce::MidiBuffer& midi, int numSamples, double sampleRate)
    {
        startedInBlock_ = false;
        if (!enabled_.load(std::memory_order_relaxed))
            return 0.0;

        // One Messages-mode snapshot per block, shared with the resolver, so a UI change can't
        // land between the two.
        const int mode = messages_.load(std::memory_order_relaxed);
        blockTicks_     = mode != 1;
        blockTransport_ = mode != 0;
        const bool doTick      = blockTicks_;
        const bool doTransport = blockTransport_;

        const double blockBeatPos = beatOf(pulses_);

        // Walk the block's real-time messages: transport changes, then tempo + beat per tick.
        for (const auto& msgRef : midi)
        {
            const auto& m = msgRef.getMessage();
            if (m.getRawDataSize() != 1) continue;
            const juce::uint8 b  = m.getRawData()[0];
            const int         so = msgRef.samplePosition;

            if (doTransport)
            {
                if (b == 0xFA)
                {
                    pulses_ = 0;
                    startedInBlock_ = true;
                    tempo_.restartInterval();
                    isPlaying_.store(true);
                }
                else if (b == 0xFB) { tempo_.restartInterval(); isPlaying_.store(true); }
                else if (b == 0xFC) { isPlaying_.store(false); }
            }

            if (b == 0xF8)
            {
                // Pulses are timestamped on the audio sample clock (sample-accurate within the block).
                if (doTick && tempo_.onPulse((double) (sampleClock_ + so) / sampleRate))
                    bpmEst_.store(juce::jlimit(20.0, 300.0, tempo_.bpm()));
                // Song position only moves while the master plays (MIDI spec); with transport
                // messages off there is no play state, so every pulse counts. Counted in every
                // mode so switching Messages mode mid-play keeps the master's position.
                if (!doTransport || isPlaying_.load())
                    ++pulses_;
            }
        }

        sampleClock_ += numSamples;
        beatPosUI_.store(beatOf(pulses_), std::memory_order_relaxed);
        // A Start restarts the song: the block plays from bar 1, not from the pre-Start count.
        return startedInBlock_ ? 0.0 : blockBeatPos;
    }

    // Audio thread, after process(): this block's Messages-mode snapshot — ticks give tempo +
    // beat, transport messages (Start / Continue / Stop) give play state — and whether a Start
    // (0xFA) arrived in it.
    bool ticksDrive()      const { return blockTicks_; }
    bool transportDrives() const { return blockTransport_; }
    bool startedInBlock()  const { return startedInBlock_; }

private:
    // Cross-thread atomics.
    std::atomic<bool>   enabled_   { false };
    std::atomic<int>    messages_  { 2 };
    std::atomic<bool>   isPlaying_ { false };
    std::atomic<double> bpmEst_    { 0.0 };   // 0 = no estimate yet
    std::atomic<double> beatPosUI_ { 0.0 };

    // Audio-thread-only state.
    // The beat is derived from an integer pulse count (24 per quarter note) rather than summed
    // 1/24 steps, so it lands exactly on every step boundary and never drifts.
    static double beatOf(juce::int64 pulses) { return (double) pulses / 24.0; }
    juce::int64             pulses_      = 0;   // clock pulses since the last Start
    bool                    startedInBlock_ = false;
    bool                    blockTicks_     = true;    // Messages mode snapshot (default 2 = both)
    bool                    blockTransport_ = true;
    juce::int64             sampleClock_ = 0;   // samples processed while enabled (pulse timestamps)
    mu_core::MidiClockTempo tempo_;             // shared family tempo estimator
};
