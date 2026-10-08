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
// (beatPos_, sampleClock_, tempo_) must not be accessed from any other thread.
class MidiClockSync
{
public:
    // ── Message-thread setters ───────────────────────────────────────────
    void setEnabled(bool on)
    {
        enabled_.store(on, std::memory_order_relaxed);
        if (!on) isPlaying_.store(false);
    }

    void setMessages(int mode)   // 0=clock only, 1=transport only, 2=both
    {
        messages_.store(juce::jlimit(0, 2, mode), std::memory_order_relaxed);
    }

    // ── Cross-thread reads ───────────────────────────────────────────────
    bool   isEnabled()    const { return enabled_.load(std::memory_order_relaxed); }
    int    getMessages()  const { return messages_.load(std::memory_order_relaxed); }
    bool   isPlaying()    const { return isPlaying_.load(); }
    double getBpm()       const { return bpmEst_.load(); }
    double getBeatPosUI() const { return beatPosUI_.load(std::memory_order_relaxed); }

    // ── Audio thread ─────────────────────────────────────────────────────
    // Scans midi for real-time messages; updates internal state; returns the
    // start-of-block beat position (0.0 if sync is disabled).
    double process(const juce::MidiBuffer& midi, int numSamples, double sampleRate)
    {
        if (!enabled_.load(std::memory_order_relaxed))
            return 0.0;

        const bool doTick      = (messages_.load(std::memory_order_relaxed) != 1);
        const bool doTransport = (messages_.load(std::memory_order_relaxed) != 0);

        const double blockBeatPos = beatPos_;

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
                    beatPos_ = 0.0;
                    tempo_.restartInterval();
                    isPlaying_.store(true);
                }
                else if (b == 0xFB) { tempo_.restartInterval(); isPlaying_.store(true); }
                else if (b == 0xFC) { isPlaying_.store(false); }
            }

            if (doTick && b == 0xF8)
            {
                // Pulses are timestamped on the audio sample clock (sample-accurate within the block).
                if (tempo_.onPulse((double) (sampleClock_ + so) / sampleRate))
                    bpmEst_.store(juce::jlimit(20.0, 300.0, tempo_.bpm()));
                beatPos_ += 1.0 / 24.0;
            }
        }

        sampleClock_ += numSamples;
        beatPosUI_.store(beatPos_, std::memory_order_relaxed);
        return blockBeatPos;
    }

private:
    // Cross-thread atomics.
    std::atomic<bool>   enabled_   { false };
    std::atomic<int>    messages_  { 2 };
    std::atomic<bool>   isPlaying_ { false };
    std::atomic<double> bpmEst_    { 120.0 };
    std::atomic<double> beatPosUI_ { 0.0 };

    // Audio-thread-only state.
    double                  beatPos_     = 0.0;
    juce::int64             sampleClock_ = 0;   // samples processed while enabled (pulse timestamps)
    mu_core::MidiClockTempo tempo_;             // shared family tempo estimator
};
