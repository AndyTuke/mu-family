#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <cmath>

// The processing load of the audio callback: the time processBlock took divided by the time its block lasts, smoothed
// for a readout and with a slowly falling peak. A product wraps its processBlock in begin() / end(); the message
// thread reads load() and peak() (relaxed atomics, no lock, nothing allocated).
//
// It measures the processor's own work, not the device's (juce::AudioDeviceManager::getCpuUsage also counts the
// driver's callback overhead), so it is the same figure in the Standalone, in a render and in a test, and a bridge
// to mu-link that calls processBlock from its own thread is measured the same way.
namespace mu_core
{

class CpuLoadMeter
{
public:
    // Audio thread, at the top of processBlock.
    void begin() noexcept { startTicks = juce::Time::getHighResolutionTicks(); }

    // Audio thread, at the end of processBlock. `numSamples` / `sampleRate` give the time the block lasts.
    void end(int numSamples, double sampleRate) noexcept
    {
        if (numSamples <= 0 || sampleRate <= 0.0) return;
        const double busy = juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - startTicks);
        update(busy, (double) numSamples / sampleRate);
    }

    // One block's measurement (separate from end() so a test can feed known times). Load is a fraction of the
    // block's time (1.0 = the block took exactly as long as it lasts).
    void update(double busySeconds, double blockSeconds) noexcept
    {
        if (blockSeconds <= 0.0) return;
        const float instant = (float) (busySeconds / blockSeconds);

        // One-pole smoothing whose time constant is in seconds, so the readout does not depend on the block size.
        const float a = 1.0f - (float) std::exp(-blockSeconds / kSmoothSeconds);
        smoothed += a * (instant - smoothed);

        // Peak hold: jumps to a higher block at once, falls with its own slower time constant.
        const float fall = (float) std::exp(-blockSeconds / kPeakFallSeconds);
        peakHold = instant > peakHold * fall ? instant : peakHold * fall;

        loadOut.store(smoothed, std::memory_order_relaxed);
        peakOut.store(peakHold, std::memory_order_relaxed);
    }

    // Message thread. Smoothed load, 0 = idle, 1 = the callback uses all its time.
    float load() const noexcept { return loadOut.load(std::memory_order_relaxed); }
    // Message thread. The recent worst block, falling slowly.
    float peak() const noexcept { return peakOut.load(std::memory_order_relaxed); }

    // Audio thread or while stopped (prepareToPlay): forget the history.
    void reset() noexcept
    {
        smoothed = peakHold = 0.0f;
        loadOut.store(0.0f, std::memory_order_relaxed);
        peakOut.store(0.0f, std::memory_order_relaxed);
    }

private:
    static constexpr double kSmoothSeconds   = 0.5;
    static constexpr double kPeakFallSeconds = 3.0;

    juce::int64 startTicks = 0;
    float smoothed = 0.0f, peakHold = 0.0f;   // audio thread only
    std::atomic<float> loadOut { 0.0f }, peakOut { 0.0f };
};

} // namespace mu_core
