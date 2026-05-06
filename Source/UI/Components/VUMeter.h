#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

// Vertical peak-level bar calibrated to the K-system / VU convention:
//   Top of visible scale  =   0 dBFS audio (clipping point)
//   "0 VU" reference mark = −18 dBFS audio (industry standard per AES, EBU)
//   Floor                 = −60 dBFS audio
// Colour zones: green below 0 VU, yellow 0 VU → −6 dBFS, red above −6 dBFS.
// Clip LED latches at ≥ 0 dBFS, holds 3 s, click to clear.
//
// Set getLevel to a callback returning 0–1 linear amplitude (peak per block).
class VUMeter : public juce::Component, private juce::Timer
{
public:
    std::function<float()> getLevel;

    VUMeter();
    ~VUMeter() override;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    // Scale calibration (all dBFS).
    static constexpr float kFloorDb        = -60.0f;   // bottom of visible bar
    static constexpr float kZeroVuDb       = -18.0f;   // 0 VU reference mark (also green→yellow boundary)
    static constexpr float kRedThreshDb    =  -6.0f;   // yellow→red boundary
    static constexpr float kClipDb         =   0.0f;   // clip LED latches at this level

    // Ballistics
    static constexpr float kReleasePerTick =  -1.5f;   // dB/tick at 30Hz ≈ -45dB/s
    static constexpr float kPeakDecayTick  =  -0.3f;   // dB/tick at 30Hz
    static constexpr int   kPeakHoldFrames =   20;     // ~0.67 s at 30Hz
    static constexpr int   kClipHoldFrames =   90;     // 3.0 s at 30Hz

    float displayDb  = kFloorDb;
    float peakDb     = kFloorDb;
    int   peakHold   = 0;
    bool  clipLit    = false;
    int   clipHold   = 0;

    // dBFS → 0..1 for bar height (kFloorDb..0 dBFS mapped linearly).
    static float dbToNorm(float db) noexcept;
    static float linToDb(float lin) noexcept;

    void timerCallback() override;
};
