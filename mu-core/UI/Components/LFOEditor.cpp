#include "LFOEditor.h"

static constexpr float kPointRadius  = 5.0f;
static constexpr float kHitRadius    = 8.0f;
static constexpr float kSegmentHitThreshold = 10.0f;

LFOEditor::LFOEditor()
{
    // Default: two endpoints
    points.push_back({ 0.0f, 0.0f, false, 0.0f, 0.0f });
    points.push_back({ 1.0f, 0.0f, false, 0.0f, 0.0f });
}

void LFOEditor::setPoints(const std::vector<ControlSequence::CurvePoint>& pts)
{
    points = pts;
    // Defensive: clamp any out-of-range anchor y (a curve persisted before range-clamping,
    // or carried across a polarity switch) into the visible range so it can't draw off-panel.
    const float yFloor = unipolar ? 0.0f : -1.0f;
    for (auto& p : points) p.y = juce::jlimit(yFloor, 1.0f, p.y);
    repaint();
}

void LFOEditor::setPlayheadPhase(float phase)
{
    playheadPhase = phase;
    repaint();
}

juce::Point<float> LFOEditor::toScreen(float x, float y) const
{
    // CurvePoint::y is canonical -1..+1 (bipolar) or 0..1 (unipolar) — the DSP
    // multiplies by 100 in ControlSequence::evaluateSmooth to reach the matrix's
    // -100..+100 range. Display must match the data range, not the matrix range.
    const float h = (float)getHeight();
    const float screenY = unipolar
        ? (1.0f - y) * h                              // 0→bottom, 1→top
        : (1.0f - (y + 1.0f) * 0.5f) * h;             // -1→bottom, +1→top
    return { x * (float)getWidth(), screenY };
}

juce::Point<float> LFOEditor::fromScreen(float sx, float sy) const
{
    const float h = (float)getHeight();
    float x = sx / (float)getWidth();
    float y = unipolar
        ? juce::jlimit(0.0f,  1.0f, 1.0f - sy / h)
        : juce::jlimit(-1.0f, 1.0f, (1.0f - sy / h) * 2.0f - 1.0f);
    return { juce::jlimit(0.0f, 1.0f, x), y };
}

float LFOEditor::snapX(float x) const
{
    // Snap to the nearest fixed-width step boundary (k·stepFraction). No grid when the step
    // is ≥ the loop (stepFraction ≥ 1) or unset (0). The boundaries tile the loop with a
    // partial final cell — snapping clamps to [0,1] so the trailing partial cell is honoured.
    if (stepFraction <= 0.0f || stepFraction >= 1.0f)
        return x;

    return juce::jlimit(0.0f, 1.0f, std::round(x / stepFraction) * stepFraction);
}

int LFOEditor::hitSegment(juce::Point<float> screen) const
{
    if (points.size() < 2)
        return -1;

    const float px = screen.x;
    const float py = screen.y;
    const int n = (int)points.size();

    for (int i = 0; i < n - 1; ++i)
    {
        auto a = toScreen(points[i].x, points[i].y);
        auto b = toScreen(points[i + 1].x, points[i + 1].y);

        const float segLen = a.getDistanceFrom(b);
        if (segLen < 1.0f)
            continue;

        const float ux = (b.x - a.x) / segLen;
        const float uy = (b.y - a.y) / segLen;
        const float proj = (px - a.x) * ux + (py - a.y) * uy;
        if (proj < kPointRadius || proj > segLen - kPointRadius)
            continue;

        const float closestX = a.x + ux * proj;
        const float closestY = a.y + uy * proj;
        if (std::hypot(px - closestX, py - closestY) <= kSegmentHitThreshold)
            return i;
    }
    return -1;
}

int LFOEditor::hitMidpoint(juce::Point<float> screen) const
{
    for (int i = 0; i < (int)points.size() - 1; ++i)
    {
        const auto a = toScreen(points[i].x, points[i].y);
        const auto b = toScreen(points[i + 1].x, points[i + 1].y);
        const juce::Point<float> mid((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
        if (screen.getDistanceFrom(mid) <= kHitRadius)
            return i;
    }
    return -1;
}

int LFOEditor::hitTest(juce::Point<float> screen) const
{
    for (int i = 0; i < (int)points.size(); ++i)
    {
        auto s = toScreen(points[i].x, points[i].y);
        if (screen.getDistanceFrom(s) <= kHitRadius)
            return i;
    }
    return -1;
}

juce::Path LFOEditor::buildCurvePath() const
{
    if (points.size() < 2) return {};

    juce::Path path;
    auto first = toScreen(points[0].x, points[0].y);
    path.startNewSubPath(first.x, first.y);

    for (int i = 1; i < (int)points.size(); ++i)
    {
        const auto& prev = points[i - 1];
        const auto& curr = points[i];
        const auto prevS = toScreen(prev.x, prev.y);
        const auto currS = toScreen(curr.x, curr.y);

        if (isDraggingSegment && (i - 1) == segDragSegment)
        {
            // Quadratic Bezier through prevS (t=0), segDragLogical (t=0.5), currS (t=1).
            // Solving P(0.5) = M for the quadratic control point C:
            //   P(0.5) = 0.25*A + 0.5*C + 0.25*B = M  →  C = 2M - 0.5A - 0.5B
            const auto dragS = toScreen(segDragLogical.x, segDragLogical.y);
            const float cx = 2.0f * dragS.x - 0.5f * prevS.x - 0.5f * currS.x;
            const float cy = 2.0f * dragS.y - 0.5f * prevS.y - 0.5f * currS.y;
            path.quadraticTo(cx, cy, currS.x, currS.y);
        }
        else
        {
            // Smooth (Catmull-Rom) cubic through the anchors — matches
            // ControlSequence::evaluateSmooth so the drawing and the audio agree. Interior
            // tangents are centred differences; boundaries use one-sided (2-point → linear).
            // An explicit per-segment bend (handleY) adds on top.
            const int n  = (int) points.size();
            const int lo = i - 1;
            auto py = [&](int k) { return points[(size_t) juce::jlimit(0, n - 1, k)].y; };
            const float m0 = (lo == 0)         ? (py(lo + 1) - py(lo)) : 0.5f * (py(lo + 1) - py(lo - 1));
            const float m1 = (lo + 1 == n - 1) ? (py(lo + 1) - py(lo)) : 0.5f * (py(lo + 2) - py(lo));
            // Clamp the bezier control points to the visible range (bottom = 0 unipolar / -1
            // bipolar, top = 1) so the curve can't draw past the panel — the convex-hull
            // property keeps the whole segment in range. Matches ControlSequence::evaluateSmooth.
            const float yFloor = unipolar ? 0.0f : -1.0f;
            const float c1y = juce::jlimit(yFloor, 1.0f, prev.y + m0 / 3.0f + (prev.hasBezierHandle ? prev.handleY : 0.0f));
            const float c2y = juce::jlimit(yFloor, 1.0f, curr.y - m1 / 3.0f + (curr.hasBezierHandle ? curr.handleY : 0.0f));

            const float c1x = prevS.x + (currS.x - prevS.x) * 0.33f;
            const float c2x = prevS.x + (currS.x - prevS.x) * 0.66f;
            path.cubicTo(c1x, toScreen(0.0f, c1y).y, c2x, toScreen(0.0f, c2y).y, currS.x, currS.y);
        }
    }
    return path;
}

void LFOEditor::paint(juce::Graphics& g)
{
    using Id = MuLookAndFeel::ColourIds;

    g.setColour(MuLookAndFeel::colour(Id::lfoEditorBackground));
    g.fillAll();

    // Zero/baseline line
    const float zeroY = unipolar ? (float)getHeight() : toScreen(0.0f, 0.0f).y;
    g.setColour(MuLookAndFeel::colour(Id::lfoEditorZeroLine));
    g.drawHorizontalLine((int)zeroY, 0.0f, (float)getWidth());

    if (points.size() >= 2)
    {
        if (stepFraction > 0.0f && stepFraction < 1.0f)
        {
            // Tile the loop with fixed-width steps: a line at each k·stepFraction below 1.0,
            // so a step that doesn't divide the loop leaves a narrower final cell (e.g. 3/16
            // in a 16/16 loop → lines at 3,6,9,12,15 sixteenths → 5 full cells + a 1/16 cell).
            g.setColour(MuLookAndFeel::colour(Id::stepEditorGridLine));
            const float w = (float)getWidth();
            for (int k = 1; k * stepFraction < 0.9995f; ++k)
                g.drawVerticalLine((int)(k * stepFraction * w), 0.0f, (float)getHeight());
        }

        auto curve = buildCurvePath();

        // Fill under curve (from curve to zero/baseline)
        juce::Path fill = curve;
        fill.lineTo((float)getWidth(), zeroY);
        fill.lineTo(0.0f, zeroY);
        fill.closeSubPath();
        g.setColour(MuLookAndFeel::colour(Id::lfoEditorCurveFill));
        g.fillPath(fill);

        // Curve line
        g.setColour(MuLookAndFeel::colour(Id::lfoEditorCurve));
        g.strokePath(curve, juce::PathStrokeType(mu_ui::sf(1.5f)));
    }

    // Control points
    const float pointR = mu_ui::sf(kPointRadius);
    for (int i = 0; i < (int)points.size(); ++i)
    {
        auto s = toScreen(points[i].x, points[i].y);
        bool hovered = (i == dragIndex);
        g.setColour(hovered ? MuLookAndFeel::colour(Id::lfoEditorPointHover)
                            : MuLookAndFeel::colour(Id::lfoEditorPoint));
        g.fillEllipse(s.x - pointR, s.y - pointR, pointR * 2, pointR * 2);
    }

    if (isDraggingSegment && segDragSegment >= 0)
    {
        // Show the drag position as a filled handle during a segment bend drag.
        const auto dragS = toScreen(segDragLogical.x, segDragLogical.y);
        g.setColour(MuLookAndFeel::colour(Id::lfoEditorPointHover));
        g.fillEllipse(dragS.x - pointR, dragS.y - pointR, pointR * 2, pointR * 2);
    }
    else if (hoverSegment >= 0 && hoverSegment < (int)points.size() - 1)
    {
        const auto a = toScreen(points[hoverSegment].x, points[hoverSegment].y);
        const auto b = toScreen(points[hoverSegment + 1].x, points[hoverSegment + 1].y);
        const auto mid = juce::Point<float>((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
        g.setColour(MuLookAndFeel::colour(Id::lfoEditorHandle));
        g.drawEllipse(mid.x - pointR, mid.y - pointR, pointR * 2, pointR * 2, mu_ui::sf(1.5f));
    }

    // Playhead
    const float phX = playheadPhase * getWidth();
    g.setColour(MuLookAndFeel::colour(Id::lfoEditorPlayhead));
    g.drawVerticalLine((int)phX, 0.0f, (float)getHeight());
}

void LFOEditor::mouseDown(const juce::MouseEvent& e)
{
    auto pos = e.position;

    if (e.mods.isRightButtonDown())
    {
        int idx = hitTest(pos);
        // Don't remove the two boundary anchors (first/last)
        if (idx > 0 && idx < (int)points.size() - 1)
        {
            points.erase(points.begin() + idx);
            notifyChanged();
        }
        return;
    }

    dragIndex = hitTest(pos);
    const int midpointHit      = (dragIndex < 0) ? hitMidpoint(pos) : -1;
    const int hoverSegmentIndex = (dragIndex < 0 && midpointHit < 0) ? hitSegment(pos) : -1;

    if (dragIndex < 0 && midpointHit >= 0)
    {
        // Drag the midpoint handle to bend the segment — no snap, commit on mouseUp.
        const auto& pa = points[(size_t)midpointHit];
        const auto& pb = points[(size_t)midpointHit + 1];
        segDragSegment    = midpointHit;
        segDragLogical    = { (pa.x + pb.x) * 0.5f, (pa.y + pb.y) * 0.5f };
        isDraggingSegment = true;
        return;
    }

    if (dragIndex < 0 && hoverSegmentIndex < 0)
    {
        // Add a new point
        auto logPos = fromScreen(pos.x, pos.y);
        if (!e.mods.isAltDown())
            logPos.x = snapX(logPos.x);

        ControlSequence::CurvePoint newPt;
        newPt.x = logPos.x;
        newPt.y = logPos.y;
        newPt.hasBezierHandle = true;
        newPt.handleY = 0.0f;

        // Insert in sorted x order (keep first and last anchored)
        auto it = std::lower_bound(points.begin(), points.end(), newPt,
            [](const ControlSequence::CurvePoint& a, const ControlSequence::CurvePoint& b)
            { return a.x < b.x; });
        int insertPos = (int)(it - points.begin());
        insertPos = juce::jlimit(1, (int)points.size() - 1, insertPos);
        points.insert(points.begin() + insertPos, newPt);
        dragIndex = insertPos;
        notifyChanged();
    }
    else if (dragIndex < 0 && hoverSegmentIndex >= 0)
    {
        // Add a new point on the hovered segment.
        auto logPos = fromScreen(pos.x, pos.y);
        if (!e.mods.isAltDown())
            logPos.x = snapX(logPos.x);

        ControlSequence::CurvePoint newPt;
        newPt.x = logPos.x;
        newPt.y = logPos.y;
        newPt.hasBezierHandle = true;
        newPt.handleY = 0.0f;

        auto it = std::lower_bound(points.begin(), points.end(), newPt,
            [](const ControlSequence::CurvePoint& a, const ControlSequence::CurvePoint& b)
            { return a.x < b.x; });
        int insertPos = (int)(it - points.begin());
        insertPos = juce::jlimit(1, (int)points.size() - 1, insertPos);
        points.insert(points.begin() + insertPos, newPt);
        dragIndex = insertPos;
        notifyChanged();
    }
}

void LFOEditor::mouseDrag(const juce::MouseEvent& e)
{
    if (isDraggingSegment)
    {
        // Free drag — no snap. Clamp X to stay between the two flanking points.
        auto logPos = fromScreen(e.position.x, e.position.y);
        const float xMin = points[(size_t)segDragSegment].x + 0.001f;
        const float xMax = points[(size_t)segDragSegment + 1].x - 0.001f;
        logPos.x = juce::jlimit(xMin, xMax, logPos.x);
        segDragLogical = logPos;
        repaint();
        return;
    }

    if (dragIndex < 0 || dragIndex >= (int)points.size()) return;

    auto logPos = fromScreen(e.position.x, e.position.y);
    if (!e.mods.isAltDown())
        logPos.x = snapX(logPos.x);

    // Clamp x between neighbours (first/last anchors are pinned to 0/1)
    if (dragIndex == 0)
        logPos.x = 0.0f;
    else if (dragIndex == (int)points.size() - 1)
        logPos.x = 1.0f;
    else
    {
        float xMin = points[(size_t)(dragIndex - 1)].x + 0.001f;
        float xMax = points[(size_t)(dragIndex + 1)].x - 0.001f;
        logPos.x = juce::jlimit(xMin, xMax, logPos.x);
    }

    points[(size_t)dragIndex].x = logPos.x;
    points[(size_t)dragIndex].y = logPos.y;
    repaint();
}

void LFOEditor::mouseUp(const juce::MouseEvent&)
{
    if (isDraggingSegment)
    {
        // Bake the bend into bezier handles on the flanking points.
        // Cubic at t=0.5 = midY + (3/4)*handleOffset → handleOffset = (4/3)*(dragY - midY).
        auto& pa = points[(size_t)segDragSegment];
        auto& pb = points[(size_t)segDragSegment + 1];
        const float midY         = (pa.y + pb.y) * 0.5f;
        const float handleOffset = (4.0f / 3.0f) * (segDragLogical.y - midY);
        pa.hasBezierHandle = true;
        pa.handleY         = handleOffset;
        pb.hasBezierHandle = true;
        pb.handleY         = handleOffset;

        isDraggingSegment = false;
        segDragSegment    = -1;
        notifyChanged();
        return;
    }

    if (dragIndex >= 0)
        notifyChanged();
    dragIndex = -1;
}

void LFOEditor::mouseDoubleClick(const juce::MouseEvent& e)
{
    // Double-click an existing interior node to delete it (same as right-click, a more
    // discoverable gesture). The two boundary anchors (first/last, pinned to x=0/x=1) stay.
    // The first click of the double already landed on the node (hitTest > 0 → no add), so
    // there's nothing spurious to undo. Reset dragIndex so the trailing mouseUp — which the
    // second click's mouseDown set — doesn't re-notify against the now-shifted indices.
    const int idx = hitTest(e.position);
    if (idx > 0 && idx < (int) points.size() - 1)
    {
        points.erase(points.begin() + idx);
        dragIndex = -1;
        notifyChanged();
    }
}

void LFOEditor::mouseMove(const juce::MouseEvent& e)
{
    const int segment = hitSegment(e.position);
    if (segment != hoverSegment)
    {
        hoverSegment = segment;
        repaint();
    }
}

void LFOEditor::mouseExit(const juce::MouseEvent&)
{
    if (hoverSegment != -1)
    {
        hoverSegment = -1;
        repaint();
    }
}

void LFOEditor::notifyChanged()
{
    repaint();
    if (onChange) onChange(points);
}
