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
// Stereo: two thin bars, L then R, each with its own ballistics, peak-hold tick and clip latch.
// Set getLevel (left) and getLevelR (right) to callbacks returning 0–1 linear amplitude (peak per
// block). With getLevelR unset the left source is drawn on both bars, so a mono source looks the
// same and the strip layout never depends on it. Click to clear both clip latches.
class VUMeter : public juce::Component, private juce::Timer
{
public:
    enum class MeterMode { Peak, VU, K12, K14 };

    static constexpr int kBarGap = 2;   // unscaled gap between the L and R bars

    std::function<float()> getLevel;    // left (or the only) source
    std::function<float()> getLevelR;   // right source; unset = mirror the left

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

    // One bar's ballistics state.
    struct Bar
    {
        float displayDb = kFloorDb;
        float peakDb    = kFloorDb;
        int   peakHold  = 0;
        bool  clipLit   = false;
        int   clipHold  = 0;
    };

    MeterMode mode = MeterMode::Peak;
    Bar       bars[2];

    // Mode-dependent colour zone boundaries
    float refDb() const noexcept;  // green→yellow + reference mark
    float redDb() const noexcept;  // yellow→red

    static float dbToNorm(float db) noexcept;
    static float linToDb(float lin) noexcept;

    // Advance one bar by a block's incoming peak; true when it needs repainting.
    bool tick(Bar& b, float incoming) const noexcept;
    void paintBar(juce::Graphics&, const Bar&, juce::Rectangle<float> r) const;

    void timerCallback() override;
};
