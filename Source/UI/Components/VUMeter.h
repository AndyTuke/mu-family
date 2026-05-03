#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

// Vertical peak-level bar with dB-domain ballistics and clip indicator.
// Set getLevel to a callback returning 0–1 linear amplitude.
// Clip indicator lights at 0dBFS, holds 3 s, click to clear.
class VUMeter : public juce::Component, private juce::Timer
{
public:
    std::function<float()> getLevel;

    VUMeter();
    ~VUMeter() override;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    // dB values; -96dB = silence floor
    static constexpr float kFloor         = -96.0f;
    static constexpr float kReleasePerTick =  -0.05f;  // dB/tick at 30Hz ≈ -1.5dB/s
    static constexpr float kPeakDecayTick  =  -0.1f;   // dB/tick at 30Hz
    static constexpr int   kPeakHoldFrames =   75;     // 2.5 s at 30Hz
    static constexpr int   kClipHoldFrames =   90;     // 3.0 s at 30Hz

    float displayDb  = kFloor;
    float peakDb     = kFloor;
    int   peakHold   = 0;
    bool  clipLit    = false;
    int   clipHold   = 0;

    // dBFS → 0..1 for bar height mapping (linear meter scale, -48dBFS floor)
    static float dbToNorm(float db) noexcept;
    static float linToDB(float lin)  noexcept;

    void timerCallback() override;
};
