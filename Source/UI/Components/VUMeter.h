#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

// Vertical peak-level bar with decaying envelope and peak-hold marker.
// Set getLevel to a callback that returns a 0–1 linear amplitude.
class VUMeter : public juce::Component, private juce::Timer
{
public:
    std::function<float()> getLevel;

    VUMeter();
    ~VUMeter() override;

    void paint(juce::Graphics&) override;

private:
    float displayLevel  = 0.0f;
    float peakLevel     = 0.0f;
    int   peakHoldCount = 0;

    static constexpr int   kHoldFrames   = 30;    // 0.5 s at 60 Hz
    static constexpr float kDecayPerTick = 0.12f; // full decay ~140 ms at 60 Hz

    void timerCallback() override;
};
