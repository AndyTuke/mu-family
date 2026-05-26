#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <array>
#include <atomic>

// MIDI clock sync state machine, extracted from PluginProcessor.
//
// Audio thread calls process() each block; it scans the MidiBuffer for
// real-time messages (0xF8 clock tick, 0xFA/FB/FC start/continue/stop),
// maintains a 24-slot ring buffer of inter-tick intervals for BPM estimation,
// and returns the start-of-block beat position.
//
// All cross-thread reads (isEnabled, isPlaying, getBpm, getBeatPosUI) are
// backed by atomics and are safe to call from the message thread. The
// audio-thread-only fields (beatPos_, ringHead_, etc.) must not be accessed
// from any other thread.
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
        int prevTickSo = 0;

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
                    ringCount_ = 0;  samplesSinceLastTick_ = 0;
                    prevTickSo = 0;
                    isPlaying_.store(true);
                }
                else if (b == 0xFB) { isPlaying_.store(true); }
                else if (b == 0xFC) { isPlaying_.store(false); }
            }

            if (doTick && b == 0xF8)
            {
                const int interval = samplesSinceLastTick_ + (so - prevTickSo);
                if (ringCount_ > 0 && interval > 10)
                {
                    tickIntervals_[ringHead_] = interval;
                    ringHead_ = (ringHead_ + 1) % 24;
                    if (ringCount_ < 24) ++ringCount_;
                    double sum = 0.0;
                    for (int i = 0; i < ringCount_; ++i) sum += tickIntervals_[i];
                    bpmEst_.store(juce::jlimit(20.0, 300.0,
                        60.0 * sampleRate / ((sum / ringCount_) * 24.0)));
                }
                else if (ringCount_ == 0) { ++ringCount_; }
                samplesSinceLastTick_ = 0;
                prevTickSo = so;
                beatPos_ += 1.0 / 24.0;
            }
        }

        samplesSinceLastTick_ += numSamples - prevTickSo;
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
    double beatPos_              = 0.0;
    int    samplesSinceLastTick_ = 0;
    std::array<int, 24> tickIntervals_ {};
    int    ringHead_   = 0;
    int    ringCount_  = 0;
};
