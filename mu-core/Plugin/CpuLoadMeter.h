#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <cmath>

// The processing load of the audio callback: the time processBlock took divided by the time its block lasts, smoothed
// for a readout, and the worst figure of the last second (a plain average hides the spike that causes the click). A product
// wraps its processBlock in begin() / end(); the message thread reads load() and peak() (relaxed atomics, no lock,
// nothing allocated).
//
// It measures the processor's own work, not the device's (juce::AudioDeviceManager::getCpuUsage also counts the
// driver's callback overhead), so it is the same figure in the Standalone, in a render and in a test.
//
// Threads: begin() and end() belong to ONE thread at a time (the thread running processBlock; the start time is a plain
// member). A bridge that moves processBlock to its own thread must do so with the callbacks not overlapping, which is
// what it does. load() and peak() may be read from any thread.
namespace mu_core
{

class CpuLoadMeter
{
public:
    // Audio thread, at the top of processBlock.
    void begin() noexcept { startTicks = juce::Time::getHighResolutionTicks(); }

    // Audio thread, at the end of processBlock. `numSamples` / `sampleRate` give the time the block lasts. A block of no
    // frames (or a rate of 0) lasts no time, so it is not measured at all: its busy time is dropped rather than divided by zero.
    void end(int numSamples, double sampleRate) noexcept
    {
        if (numSamples <= 0 || sampleRate <= 0.0) return;
        const double busy = juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - startTicks);
        accumulate(busy, (double) numSamples / sampleRate);
    }

    // One block's measurement (separate from end() so a test can feed known times). Short blocks are summed until they
    // represent at least kMinWindowSeconds, so a block of one frame (a few microseconds of audio and a clock read of the
    // same size) does not read as a thousand percent: the figure is always busy time over a window long enough to mean something.
    void accumulate(double busySeconds, double blockSeconds) noexcept
    {
        if (blockSeconds <= 0.0) return;
        pendingBusy += busySeconds;
        pendingBlock += blockSeconds;
        if (pendingBlock < kMinWindowSeconds) return;
        update(pendingBusy, pendingBlock);
        pendingBusy = pendingBlock = 0.0;
    }

    // Message thread. Smoothed load, 0 = idle, 1 = the callback uses all its time.
    float load() const noexcept { return loadOut.load(std::memory_order_relaxed); }
    // Message thread. The highest figure of the last second.
    float peak() const noexcept { return peakOut.load(std::memory_order_relaxed); }

    // While stopped (prepareToPlay): forget the history.
    void reset() noexcept
    {
        smoothed = peakHold = 0.0f;
        holdAge = 0.0;
        pendingBusy = pendingBlock = 0.0;
        loadOut.store(0.0f, std::memory_order_relaxed);
        peakOut.store(0.0f, std::memory_order_relaxed);
    }

    static constexpr double kMinWindowSeconds = 0.003;   // figures are made from at least this much audio time
    static constexpr double kPeakHoldSeconds  = 1.0;     // how long the worst figure is kept

private:
    // One window's figure: busy time over the audio time it covers (a fraction; 1.0 = the callback used all of its time).
    void update(double busySeconds, double windowSeconds) noexcept
    {
        const float instant = (float) (busySeconds / windowSeconds);

        // One-pole smoothing whose time constant is in seconds, so the readout does not depend on the block size.
        const float a = 1.0f - (float) std::exp(-windowSeconds / kSmoothSeconds);
        smoothed += a * (instant - smoothed);

        // Peak hold: a higher figure takes over at once and restarts the hold; a figure that is not higher leaves the peak
        // alone until it has been held for a whole second, then the peak becomes the current figure.
        if (instant >= peakHold || holdAge >= kPeakHoldSeconds) { peakHold = instant; holdAge = 0.0; }
        else holdAge += windowSeconds;

        loadOut.store(smoothed, std::memory_order_relaxed);
        peakOut.store(peakHold, std::memory_order_relaxed);
    }

    static constexpr double kSmoothSeconds = 0.5;

    juce::int64 startTicks = 0;
    // Audio thread only.
    double pendingBusy = 0.0, pendingBlock = 0.0, holdAge = 0.0;
    float smoothed = 0.0f, peakHold = 0.0f;
    std::atomic<float> loadOut { 0.0f }, peakOut { 0.0f };
};

} // namespace mu_core
