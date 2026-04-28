#include "LFOEditor.h"

static constexpr float kPointRadius  = 5.0f;
static constexpr float kHitRadius    = 8.0f;

LFOEditor::LFOEditor()
{
    // Default: two endpoints
    points.push_back({ 0.0f, 0.0f });
    points.push_back({ 1.0f, 0.0f });
}

void LFOEditor::setPoints(const std::vector<ControlSequence::CurvePoint>& pts)
{
    points = pts;
    repaint();
}

void LFOEditor::setPlayheadPhase(float phase)
{
    playheadPhase = phase;
    repaint();
}

juce::Point<float> LFOEditor::toScreen(float x, float y) const
{
    // x: 0..1 -> 0..width, y: -100..+100 -> height..0 (top=+100)
    return { x * getWidth(), (1.0f - (y + 100.0f) / 200.0f) * getHeight() };
}

juce::Point<float> LFOEditor::fromScreen(float sx, float sy) const
{
    float x = sx / getWidth();
    float y = (1.0f - sy / getHeight()) * 200.0f - 100.0f;
    return { juce::jlimit(0.0f, 1.0f, x), juce::jlimit(-100.0f, 100.0f, y) };
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
        auto pt = toScreen(points[i].x, points[i].y);
        path.lineTo(pt.x, pt.y);
    }
    return path;
}

void LFOEditor::paint(juce::Graphics& g)
{
    using Id = MuClidLookAndFeel::ColourIds;

    g.setColour(MuClidLookAndFeel::colour(Id::lfoEditorBackground));
    g.fillAll();

    // Zero line
    auto zero = toScreen(0.0f, 0.0f);
    g.setColour(MuClidLookAndFeel::colour(Id::lfoEditorZeroLine));
    g.drawHorizontalLine((int)zero.y, 0.0f, (float)getWidth());

    if (points.size() >= 2)
    {
        auto curve = buildCurvePath();

        // Fill under curve
        juce::Path fill = curve;
        fill.lineTo((float)getWidth(), zero.y);
        fill.lineTo(0.0f, zero.y);
        fill.closeSubPath();
        g.setColour(MuClidLookAndFeel::colour(Id::lfoEditorCurveFill));
        g.fillPath(fill);

        // Curve line
        g.setColour(MuClidLookAndFeel::colour(Id::lfoEditorCurve));
        g.strokePath(curve, juce::PathStrokeType(1.5f));
    }

    // Control points
    for (int i = 0; i < (int)points.size(); ++i)
    {
        auto s = toScreen(points[i].x, points[i].y);
        bool hovered = (i == dragIndex);
        g.setColour(hovered ? MuClidLookAndFeel::colour(Id::lfoEditorPointHover)
                            : MuClidLookAndFeel::colour(Id::lfoEditorPoint));
        g.fillEllipse(s.x - kPointRadius, s.y - kPointRadius,
                      kPointRadius * 2, kPointRadius * 2);
    }

    // Playhead
    const float phX = playheadPhase * getWidth();
    g.setColour(MuClidLookAndFeel::colour(Id::lfoEditorPlayhead));
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

    if (dragIndex < 0)
    {
        // Add a new point
        auto logPos = fromScreen(pos.x, pos.y);
        ControlSequence::CurvePoint newPt;
        newPt.x = logPos.x;
        newPt.y = logPos.y;

        // Insert in sorted x order (keep first and last anchored)
        auto it = std::lower_bound(points.begin(), points.end(), newPt,
            [](const ControlSequence::CurvePoint& a, const ControlSequence::CurvePoint& b)
            { return a.x < b.x; });
        int insertPos = (int)(it - points.begin());
        // Do not allow inserting before idx 0 or at/after the last
        insertPos = juce::jlimit(1, (int)points.size() - 1, insertPos);
        points.insert(points.begin() + insertPos, newPt);
        dragIndex = insertPos;
        notifyChanged();
    }
}

void LFOEditor::mouseDrag(const juce::MouseEvent& e)
{
    if (dragIndex < 0 || dragIndex >= (int)points.size()) return;

    auto logPos = fromScreen(e.position.x, e.position.y);

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
    if (dragIndex >= 0)
        notifyChanged();
    dragIndex = -1;
}

void LFOEditor::notifyChanged()
{
    repaint();
    if (onChange) onChange(points);
}
