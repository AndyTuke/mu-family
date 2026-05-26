#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

// Vertical level meter with selectable mode (Peak / VU / K-12 / K-14).
//
// Peak  — instant attack, −45 dB/s release, peak-hold tick. 0 dBFS at top,
//         colour zones relative to −18 dBFS (0 VU) and −6 dBFS.
// VU    — 300 ms symmetric IIR ballistics. Same scale/zones as Peak.
// K-12  — 300 ms IIR. Green→yellow at −12 dBFS (0 K), yellow→red at −8 dBFS.
// K-14  — 300 ms IIR. Green→yellow at −14 dBFS (0 K), yellow→red at −10 dBFS.
//
// Set getLevel to a callback returning 0–1 linear amplitude (peak per block).
// Click to clear the latched clip indicator.
class VUMeter : public juce::Component, private juce::Timer
{
public:
    enum class MeterMode { Peak, VU, K12, K14 };

    std::function<float()> getLevel;

    VUMeter();
    ~VUMeter() override;

    void setMode(MeterMode m) { mode = m; }
    MeterMode getMode() const noexcept { return mode; }

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    static constexpr float kFloorDb        = -60.0f;
    static constexpr float kClipDb         =   0.0f;

    // Peak-mode ballistics
    static constexpr float kReleasePerTick =  -1.5f;   // dB/tick @ 30 Hz ≈ −45 dB/s
    static constexpr float kPeakDecayTick  =  -0.3f;
    static constexpr int   kPeakHoldFrames =   20;      // ~0.67 s
    static constexpr int   kClipHoldFrames =   90;      // 3.0 s

    // VU / K-mode ballistics: symmetric IIR, τ = 300 ms @ 30 Hz
    // α = exp(−1 / (0.3 × 30)) ≈ 0.895
    static constexpr float kVuAlpha = 0.895f;

    MeterMode mode     = MeterMode::Peak;
    float displayDb    = kFloorDb;
    float peakDb       = kFloorDb;
    int   peakHold     = 0;
    bool  clipLit      = false;
    int   clipHold     = 0;

    // Mode-dependent colour zone boundaries
    float refDb() const noexcept;  // green→yellow + reference mark
    float redDb() const noexcept;  // yellow→red

    static float dbToNorm(float db) noexcept;
    static float linToDb(float lin) noexcept;

    void timerCallback() override;
};
