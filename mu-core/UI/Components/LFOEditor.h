#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "MuClidLookAndFeel.h"
#include "Sequencer/ControlSequence.h"

// Serum/Vital-style drawable curve editor.
// Click = add point, drag = move point, right-click = remove point.
// Segments are linear in v1; bezier handles stored in CurvePoint for v2.
// Playhead position set by caller for live playback display.
class LFOEditor : public juce::Component
{
public:
    std::function<void(const std::vector<ControlSequence::CurvePoint>&)> onChange;

    LFOEditor();

    void setPoints(const std::vector<ControlSequence::CurvePoint>& pts);
    const std::vector<ControlSequence::CurvePoint>& getPoints() const noexcept { return points; }

    // Unipolar: curve spans 0..+100 (bottom=0); bipolar: -100..+100 (centre=0, default).
    void setUnipolar(bool u) { unipolar = u; repaint(); }

    // playhead: 0.0 = start, 1.0 = end of loop
    void setPlayheadPhase(float phase);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    std::vector<ControlSequence::CurvePoint> points;
    float playheadPhase = 0.0f;
    int   dragIndex = -1;
    bool  unipolar  = false;

    juce::Point<float> toScreen(float x, float y) const;
    juce::Point<float> fromScreen(float sx, float sy) const;
    int hitTest(juce::Point<float> screen) const;
    void notifyChanged();
    juce::Path buildCurvePath() const;
};
