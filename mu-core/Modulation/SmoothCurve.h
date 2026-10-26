#pragma once

#include "Sequencer/ControlSequence.h"

#include <algorithm>
#include <cmath>
#include <vector>

// The Smooth modulator curve: a cubic through every anchor that never swings past its neighbours. The audio evaluator
// (ControlSequence::evaluateSmooth) and the editor's drawing (LFOEditor::buildCurvePath) both build each segment as a
// cubic Bezier from the control heights below, so what is drawn is what runs.
//
// Tangents are the monotone-cubic (Fritsch-Carlson 1980, as in PCHIP) kind: at an anchor the slope is the harmonic mean of the
// two neighbouring chord slopes, and zero at a peak or a trough, so a segment between two anchors stays between their
// heights. A plain Catmull-Rom tangent used the anchors' heights without their spacing, so anchors close together beside a
// long segment made it swing past the points (loops and humps between them).
namespace mu_mod
{

// The y-slope (per unit of x) the curve has at anchor `i` of `pts` (sorted by x, at least two points).
inline float smoothSlopeAt(const std::vector<ControlSequence::CurvePoint>& pts, int i)
{
    const int n = (int) pts.size();
    auto chord = [&](int k)   // slope of the straight line from anchor k to k + 1
    {
        const float dx = pts[(size_t) k + 1].x - pts[(size_t) k].x;
        return dx > 1.0e-6f ? (pts[(size_t) k + 1].y - pts[(size_t) k].y) / dx : 0.0f;
    };
    if (i <= 0)     return chord(0);        // the ends follow their one chord, so two anchors stay a straight line
    if (i >= n - 1) return chord(n - 2);
    const float a = chord(i - 1), b = chord(i);
    if (a * b <= 0.0f) return 0.0f;         // a peak, a trough or a flat run: level at the anchor
    const float h0 = pts[(size_t) i].x - pts[(size_t) i - 1].x, h1 = pts[(size_t) i + 1].x - pts[(size_t) i].x;
    const float w0 = 2.0f * h1 + h0, w1 = h1 + 2.0f * h0;   // weighted harmonic mean (Fritsch-Butland) for uneven spacing
    return (w0 + w1) / (w0 / a + w1 / b);
}

// The Bezier control heights of segment `seg` (anchor seg to seg + 1), with the segment's own bend added, kept inside
// [yFloor, 1]. The control x positions are a third and two thirds along the segment.
inline void smoothControlY(const std::vector<ControlSequence::CurvePoint>& pts, int seg, float yFloor, float& c1y, float& c2y)
{
    const auto& p0 = pts[(size_t) seg];
    const auto& p1 = pts[(size_t) seg + 1];
    const float len = p1.x - p0.x;
    c1y = std::clamp(p0.y + smoothSlopeAt(pts, seg)     * len / 3.0f + (p0.hasBezierHandle ? p0.handleY : 0.0f), yFloor, 1.0f);
    c2y = std::clamp(p1.y - smoothSlopeAt(pts, seg + 1) * len / 3.0f + (p1.hasBezierHandle ? p1.handleY : 0.0f), yFloor, 1.0f);
}

} // namespace mu_mod
