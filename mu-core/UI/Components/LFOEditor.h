#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "MuLookAndFeel.h"
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
    void setUnipolar(bool u)
    {
        unipolar = u;
        // Re-clamp stored anchors into the new visible range — a bipolar→unipolar switch can
        // otherwise leave negative y below the unipolar floor, drawing the curve off-panel.
        const float yFloor = u ? 0.0f : -1.0f;
        for (auto& p : points) p.y = juce::jlimit(yFloor, 1.0f, p.y);
        repaint();
    }
    // Step grid as a fraction of the loop (step/loop, 0..1). The grid tiles the editor with
    // fixed-width cells of this fraction + a partial final cell (so a 3/16 step in a 16/16 loop
    // shows 5 full cells + a 1/16 remainder), and X-snap lands on the cell boundaries. 0 or ≥1
    // = no grid / no snap. Replaces the old equal-`stepCount`-division model.
    void setStepFraction(float frac) { stepFraction = frac; repaint(); }

    // playhead: 0.0 = start, 1.0 = end of loop
    void setPlayheadPhase(float phase);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

private:
    float snapX(float x) const;
    int   hitSegment(juce::Point<float> screen) const;
    int   hitMidpoint(juce::Point<float> screen) const;
    std::vector<ControlSequence::CurvePoint> points;
    float playheadPhase = 0.0f;
    int   dragIndex = -1;
    float stepFraction = 0.0f;   // one step as a fraction of the loop; 0/≥1 = no grid
    int   hoverSegment = -1;
    bool  unipolar  = false;

    // Segment bend drag: drag the midpoint handle of a segment freely to
    // preview a quadratic curve; on release the bend is baked into bezier
    // handles on the flanking points (no new anchor inserted).
    int                segDragSegment  = -1;
    bool               isDraggingSegment = false;
    juce::Point<float> segDragLogical  { 0.0f, 0.0f };

    juce::Point<float> toScreen(float x, float y) const;
    juce::Point<float> fromScreen(float sx, float sy) const;
    int hitTest(juce::Point<float> screen) const;
    void notifyChanged();
    juce::Path buildCurvePath() const;
};
