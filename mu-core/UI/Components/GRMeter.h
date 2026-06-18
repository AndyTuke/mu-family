#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <cmath>
#include "MuLookAndFeel.h"

// Narrow inverted bar showing sidechain gain reduction.
// Fills top→down: empty = no GR, full = maximum ducking.
// Driven by a getGR callback returning 0–1 linear GR depth.
class GRMeter : public juce::Component, private juce::Timer
{
public:
    std::function<float()> getGR;

    GRMeter()  { startTimerHz(mu_ui::kUiRefreshHz); }
    ~GRMeter() override { stopTimer(); }

    void paint(juce::Graphics& g) override
    {
        // Meter is completely invisible when no GR is being applied; the slot
        // only appears once ducking is active so it cannot be mistaken for a
        // permanent indicator that does not respond to the sidechain signal.
        if (displayGR <= kVisibleThreshold) return;

        const float w = (float)getWidth();
        const float h = (float)getHeight();

        g.setColour(MuLookAndFeel::colour(MuLookAndFeel::indicatorGRMeterBg));
        g.fillRoundedRectangle(0.0f, 0.0f, w, h, 2.0f);

        g.setColour(MuLookAndFeel::colour(MuLookAndFeel::indicatorGRMeterBar));
        g.fillRect(0.0f, 0.0f, w, displayGR * h);

        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.drawRoundedRectangle(0.0f, 0.0f, w, h, 2.0f, 1.0f);
    }

private:
    float displayGR = 0.0f;
    // Fast attack (~1 frame), ~220 ms release at 30 Hz
    static constexpr float kRelease         = 0.85f;
    static constexpr float kVisibleThreshold = 0.005f;

    void timerCallback() override
    {
        const float incoming = getGR ? getGR() : 0.0f;
        const float prev = displayGR;
        displayGR = (incoming > displayGR)
                  ? incoming
                  : kRelease * displayGR + (1.0f - kRelease) * incoming;
        displayGR = juce::jlimit(0.0f, 1.0f, displayGR);

        // Repaint on meaningful value change, OR whenever visibility flips,
        // so the final fade to invisible is never lost to the delta threshold.
        const bool wasVisible = prev       > kVisibleThreshold;
        const bool nowVisible = displayGR  > kVisibleThreshold;
        if (wasVisible != nowVisible || std::abs(displayGR - prev) > 0.001f)
            repaint();
    }
};
