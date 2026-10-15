#include "StepEditor.h"
#include <cmath>

StepEditor::StepEditor()
{
    steps.assign(8, 0.0f);
}

void StepEditor::setSteps(const std::vector<float>& values)
{
    steps = values;
    repaint();
}

void StepEditor::setStepCount(int count)
{
    if (count < 1 || count > 4096) { jassertfalse; return; }
    steps.resize((size_t)count, 0.0f);
    repaint();
}

void StepEditor::setLevelCells(int n)
{
    n = (n >= 3) ? 3 : (n == 2 ? 2 : 0);
    if (n == levelCells) return;
    levelCells = n;
    repaint();
}

int StepEditor::levelOfValue(float v) const noexcept
{
    if (levelCells < 2) return 0;
    return juce::jlimit(0, levelCells - 1, (int) std::lround(v / 100.0f * (float) (levelCells - 1)));
}

float StepEditor::valueOfLevel(int level) const noexcept
{
    if (levelCells < 2) return 0.0f;
    return 100.0f * (float) juce::jlimit(0, levelCells - 1, level) / (float) (levelCells - 1);
}

void StepEditor::setBarColour(juce::Colour c)
{
    if (c == barColour) return;
    barColour = c;
    repaint();
}

// Repaint only what the playhead changes: in the metal look the lit step's old and new
// cells (and only when the step changes), otherwise the strips under the old and new line.
void StepEditor::setPlayheadPhase(float phase)
{
    const float oldPhase = playheadPhase;
    playheadPhase = phase;

    if (MuLookAndFeel::isMetal(*this))
    {
        const int oldStep = stepAtPhase(oldPhase);
        const int newStep = stepAtPhase(phase);
        if (oldStep == newStep) return;
        for (int i : { oldStep, newStep })
            if (i >= 0)
                repaint(juce::Rectangle<float>(cellEdge(i), 0.0f, cellEdge(i + 1) - cellEdge(i), (float) getHeight())
                            .getSmallestIntegerContainer());
        return;
    }

    const int oldX = (int) (oldPhase * (float) getWidth());
    const int newX = (int) (phase    * (float) getWidth());
    if (oldX == newX) return;
    repaint(oldX - 1, 0, 3, getHeight());
    repaint(newX - 1, 0, 3, getHeight());
}

// Left edge (px) of cell i; i == count gives the right edge of the last cell. Cells tile by
// the step fraction with a narrower partial final cell when set, else equal 1/n cells —
// matching the audio (evaluateStepped) and the smooth editor's grid.
float StepEditor::cellEdge(int i) const
{
    const int  n     = juce::jmax(1, (int) steps.size());
    const bool tiled = (stepFraction > 0.0f && stepFraction < 1.0f);
    return (tiled ? juce::jmin(1.0f, (float) i * stepFraction) : (float) i / (float) n) * (float) getWidth();
}

// The step the playhead is in at this phase, or -1 when there are no steps.
int StepEditor::stepAtPhase(float phase) const
{
    const float x = phase * (float) getWidth();
    for (int i = 0; i < (int) steps.size(); ++i)
        if (x >= cellEdge(i) && x < cellEdge(i + 1)) return i;
    return -1;
}

int StepEditor::hitStepIndex(int x) const
{
    const int n = (int)steps.size();
    if (n == 0 || getWidth() <= 0) return -1;
    const float px = (float)x / (float)getWidth();   // 0..1 across the loop
    // Tile by step width (so a click in the narrow final cell still resolves to it) or
    // fall back to equal cells when no fraction is set.
    const int idx = (stepFraction > 0.0f && stepFraction < 1.0f)
                  ? (int)(px / stepFraction)
                  : (int)(px * (float)n);
    return juce::jlimit(0, n - 1, idx);
}

float StepEditor::yToValue(int y) const
{
    const float norm = 1.0f - (float)y / (float)getHeight(); // 0=bottom, 1=top
    float v;
    if (unipolar)
        v = juce::jlimit(0.0f, 100.0f, norm * 100.0f);
    else
        v = juce::jlimit(-100.0f, 100.0f, (norm - 0.5f) * 200.0f);

    if (quantizeLevels > 1)
    {
        const float lo   = unipolar ? 0.0f : -100.0f;
        const float hi   = 100.0f;
        const float step = (hi - lo) / (float)(quantizeLevels - 1);
        v = lo + step * std::round((v - lo) / step);
        v = juce::jlimit(lo, hi, v);
    }
    return v;
}

// Level-cell mode: write one cell's level (no-op when unchanged) and tell the owner.
void StepEditor::setCellLevel(int idx, int level)
{
    if (idx < 0 || idx >= (int) steps.size()) return;
    const float v = valueOfLevel(level);
    if (levelOfValue(steps[(size_t) idx]) != level)
    {
        steps[(size_t) idx] = v;
        repaint();
        if (onStepChanged) onStepChanged(idx, v);
    }
}

// Level-cell mode: tell the owner which cell the mouse is on and its level (for the status bar).
void StepEditor::reportCell(int x)
{
    const int idx = hitStepIndex(x);
    if (idx >= 0 && onCellStatus) onCellStatus(idx, levelOfValue(steps[(size_t) idx]));
}

void StepEditor::applyAt(int x, int y)
{
    int idx = hitStepIndex(x);
    if (idx < 0) return;
    float v = yToValue(y);
    steps[(size_t)idx] = v;
    repaint();
    if (onStepChanged) onStepChanged(idx, v);
}

void StepEditor::paint(juce::Graphics& g)
{
    using Id = MuLookAndFeel::ColourIds;

    const int n = (int)steps.size();
    if (MuLookAndFeel::isMetal(*this))
    {
        paintLamps(g);
        return;
    }

    g.setColour(MuLookAndFeel::colour(Id::stepEditorBackground));
    g.fillAll();

    if (n == 0) return;

    const float h = (float)getHeight();

    auto cellL = [&](int i){ return cellEdge(i); };
    auto cellR = [&](int i){ return cellEdge(i + 1); };
    auto barW  = [&](int i){ return juce::jmax(1.0f, cellR(i) - cellL(i) - 2.0f); };

    // Grid dividers at each internal cell boundary
    g.setColour(MuLookAndFeel::colour(Id::stepEditorGridLine));
    for (int i = 1; i < n; ++i)
        g.drawVerticalLine((int)cellL(i), 0, h);

    // Bars
    if (unipolar)
    {
        for (int i = 0; i < n; ++i)
        {
            const float v    = juce::jmax(0.0f, steps[(size_t)i]); // clamp to 0..100
            const float barH = (v / 100.0f) * h;
            g.setColour(barColour.withAlpha(0.85f));
            g.fillRect(cellL(i) + 1.0f, h - barH, barW(i), barH);
        }
    }
    else
    {
        const float centY = h * 0.5f;
        for (int i = 0; i < n; ++i)
        {
            const float v      = steps[(size_t)i]; // -100..+100
            const float norm   = v / 200.0f;       // -0.5..+0.5
            const float barH   = std::abs(norm) * h;
            const float barTop = (norm >= 0.0f) ? centY - barH : centY;
            g.setColour(barColour.withAlpha(0.85f));
            g.fillRect(cellL(i) + 1.0f, barTop, barW(i), barH);
        }
        // Centre zero line (bipolar only)
        g.setColour(MuLookAndFeel::colour(Id::stepEditorZeroLine));
        g.drawHorizontalLine((int)(h * 0.5f), 0.0f, (float)getWidth());
    }

    // Bottom zero line (unipolar) or centre zero line (already drawn for bipolar above)
    if (unipolar)
    {
        g.setColour(MuLookAndFeel::colour(Id::stepEditorZeroLine));
        g.drawHorizontalLine((int)(h - 1), 0.0f, (float)getWidth());
    }

    // Playhead
    const float phX = playheadPhase * (float)getWidth();
    g.setColour(juce::Colours::white.withAlpha(0.5f));
    g.drawVerticalLine((int)phX, 0.0f, h);
}

// Metal style: the display of lamps the rhythm rings use — each step a lens over the dark
// lamp base, lit (with a hot spot) as far as its value reaches; the step under the playhead
// lit brighter, as on the ring.
void StepEditor::paintLamps(juce::Graphics& g)
{
    const auto& L = MuLookAndFeel::lighting();
    g.fillAll(MuLookAndFeel::lampBase().darker(L.lampGapDarken));   // the gaps between lenses

    const int n = (int)steps.size();
    if (n == 0) return;

    const float w = (float)getWidth();
    const float h = (float)getHeight();
    auto cellL = [&](int i){ return cellEdge(i); };
    auto cellR = [&](int i){ return cellEdge(i + 1); };
    const int current = stepAtPhase(playheadPhase);

    const float gap    = juce::jmax(1.0f, mu_ui::sf((float) MuLookAndFeel::kLampCellGap));
    const float corner = mu_ui::sf((float) MuLookAndFeel::kLampCellCorner);
    for (int i = 0; i < n; ++i)
    {
        const auto cell = juce::Rectangle<float>(cellL(i) + gap * 0.5f, gap, cellR(i) - cellL(i) - gap, h - gap * 2.0f);
        if (cell.getWidth() <= 0.0f) continue;

        // Unlit lens: the whole cell.
        g.setColour(MuLookAndFeel::lampColour(barColour, L.lampOff));
        g.fillRoundedRectangle(cell, corner);

        // Level cells: the whole lens lit to the level's strength; the playhead a ring round the cell.
        if (levelCells > 0)
        {
            const int level = levelOfValue(steps[(size_t) i]);
            const float strength = level <= 0 ? L.lampOff : (level == 1 ? L.lampOn : L.lampAccent);
            if (level > 0)
            {
                juce::Path lens;
                lens.addRoundedRectangle(cell, corner);
                MuLookAndFeel::drawLamp(g, lens, {}, MuLookAndFeel::lampColour(barColour, strength),
                                        cell.getCentre(), juce::jmax(cell.getWidth(), cell.getHeight()) * L.lampHotSpotReach);
            }
            if (i == current)
            {
                g.setColour(juce::Colours::white.withAlpha(L.lampPlayheadRing));
                g.drawRoundedRectangle(cell.reduced(0.5f), corner, juce::jmax(1.0f, mu_ui::sf(1.5f)));
            }
            continue;
        }

        // Lit part: up from the bottom (unipolar) or out from the centre (bipolar).
        const float v = steps[(size_t)i];
        juce::Rectangle<float> lit;
        if (unipolar)
        {
            const float litH = juce::jmax(0.0f, v) / 100.0f * cell.getHeight();
            lit = cell.withTop(cell.getBottom() - litH);
        }
        else
        {
            const float mid  = cell.getCentreY();
            const float litH = std::abs(v) / 200.0f * cell.getHeight();
            lit = v >= 0.0f ? juce::Rectangle<float>(cell.getX(), mid - litH, cell.getWidth(), litH)
                            : juce::Rectangle<float>(cell.getX(), mid, cell.getWidth(), litH);
        }
        if (lit.getHeight() < 0.5f) continue;

        const float on   = L.lampOn + (i == current ? L.lampPlayhead : 0.0f);
        const auto  lens = MuLookAndFeel::lampColour(barColour, juce::jmin(1.0f, on));
        juce::Path shape;
        shape.addRoundedRectangle(lit, corner);
        MuLookAndFeel::drawLamp(g, shape, {}, lens, lit.getCentre(), juce::jmax(lit.getWidth(), lit.getHeight()) * L.lampHotSpotReach);
    }

    // Bipolar: a faint centre line marks zero between the lenses.
    if (! unipolar)
    {
        g.setColour(juce::Colours::black.withAlpha(L.shadow(L.ringTrack)));
        g.drawHorizontalLine((int)(h * 0.5f), 0.0f, w);
    }
}

void StepEditor::mouseDown(const juce::MouseEvent& e)
{
    if (levelCells > 0)
    {
        // First click of a gesture: step the cell's level (back with Shift) and remember it to paint on.
        const int idx = hitStepIndex(e.x);
        if (idx < 0) return;
        const int cur = levelOfValue(steps[(size_t) idx]);
        paintLevel    = (cur + (e.mods.isShiftDown() ? levelCells - 1 : 1)) % levelCells;
        lastPaintCell = idx;
        setCellLevel(idx, paintLevel);
        reportCell(e.x);
        return;
    }
    applyAt(e.x, e.y);
}

void StepEditor::mouseDrag(const juce::MouseEvent& e)
{
    if (levelCells > 0)
    {
        // Paint the same state into each new cell entered.
        const int idx = hitStepIndex(e.x);
        if (idx >= 0 && idx != lastPaintCell) { lastPaintCell = idx; setCellLevel(idx, paintLevel); }
        reportCell(e.x);
        return;
    }
    applyAt(e.x, e.y);
}

void StepEditor::mouseMove(const juce::MouseEvent& e)
{
    if (levelCells > 0) reportCell(e.x);
}

void StepEditor::mouseUp(const juce::MouseEvent&)
{
    lastPaintCell = -1;
}
