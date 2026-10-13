#include "StepRows.h"
#include "Sequencer/GrooveSequencer.h"
#include "UI/Components/MuLookAndFeel.h"

namespace mu_on
{

StepRows::StepRows(PluginProcessor& processor)
    : proc(processor)
{
    startTimerHz(mu_ui::kUiRefreshHz);   // playhead
}

void StepRows::setSelected(int idx)
{
    selected = idx;
    repaint();
}

int StepRows::rowHeight() const noexcept
{
    return getHeight() / mu_limits::kMaxLayers;
}

juce::Rectangle<int> StepRows::rowArea(int row) const
{
    return { 0, row * rowHeight(), getWidth(), rowHeight() };
}

juce::Rectangle<int> StepRows::cellsArea(int row) const
{
    return rowArea(row).withTrimmedLeft(mu_ui::s(kLabelW));
}

juce::Colour StepRows::layerColour(int row) const
{
    return MuLookAndFeel::channelPalette[
        (size_t) juce::jlimit(0, MuLookAndFeel::kChannelPaletteSize - 1, proc.getLayerColourIndex(row))];
}

bool StepRows::cellAt(juce::Point<int> p, int& row, int& step) const
{
    const int rh = rowHeight();
    if (rh <= 0) return false;
    row = p.y / rh;
    if (row < 0 || row >= proc.getNumLayers()) return false;
    const auto cells = cellsArea(row);
    if (! cells.contains(p)) return false;
    step = juce::jlimit(0, StepPattern::kNumSteps - 1, (int) ((p.x - cells.getX()) / (cells.getWidth() / (float) StepPattern::kNumSteps)));
    return true;
}

juce::uint64 StepRows::signature() const
{
    // Count, then each live row's colour and engine, folded into one number.
    juce::uint64 h = (juce::uint64) proc.getNumLayers();
    for (int i = 0; i < proc.getNumLayers(); ++i)
        h = h * 131u + (juce::uint64) (proc.getLayerColourIndex(i) * 8 + toIndex(proc.engineOf(i)));
    return h;
}

void StepRows::paint(juce::Graphics& g)
{
    for (int row = 0; row < proc.getNumLayers(); ++row)
        paintRow(g, row);
}

// One row: the name gutter, then the 16 steps as lamps behind dark lenses — on steps lit, accents
// brighter at the centre, beat starts marked by a hint of colour; empty steps stay dark even under the
// playhead (family lamp rule). A Rumble row shows the same lenses unlit and the whole row dimmed.
void StepRows::paintRow(juce::Graphics& g, int row)
{
    const auto& L = MuLookAndFeel::lighting();
    const auto col = layerColour(row);
    const bool isRumble = proc.engineOf(row) == EngineType::Rumble;
    const auto area = rowArea(row);

    const juce::Graphics::ScopedSaveState state(g);
    if (isRumble) g.beginTransparencyLayer(0.45f);

    // Name gutter: "1  Kick" in the layer's colour.
    g.setFont(juce::Font(juce::FontOptions(mu_ui::sf(11.0f), juce::Font::bold)));
    const auto label = juce::String(row + 1) + "  " + proc.getLayerName(row);
    MuLookAndFeel::drawEngravedText(g, label, area.withWidth(mu_ui::s(kLabelW)).reduced(mu_ui::s(2), 0),
                                    juce::Justification::centredLeft, col, false);

    const auto cells = cellsArea(row);
    const float cellW = cells.getWidth() / (float) StepPattern::kNumSteps;
    for (int s = 0; s < StepPattern::kNumSteps; ++s)
    {
        const juce::Rectangle<float> cell((float) cells.getX() + s * cellW + 1.5f, (float) cells.getY() + 1.5f,
                                          cellW - 3.0f, (float) cells.getHeight() - 3.0f);
        const bool on     = ! isRumble && proc.pattern().isOn(row, s);
        const bool isBeat = (s % 4) == 0;
        float lit = on ? L.lampOn : (isBeat ? L.lampBeat : L.lampOff);
        if (on && s == playheadStep) lit = juce::jmin(1.0f, lit + L.lampPlayhead);

        const auto lens = MuLookAndFeel::lampColour(col, lit);
        juce::Path shape;
        shape.addRoundedRectangle(cell, 3.0f);
        if (on)
        {
            MuLookAndFeel::drawLamp(g, shape, {}, lens, cell.getCentre(), juce::jmax(cell.getWidth(), cell.getHeight()) * 0.6f);
            if (proc.pattern().isAccent(row, s))   // accent: a hotter centre
            {
                g.setColour(lens.brighter(L.lampHot * 2.0f));
                g.fillRoundedRectangle(cell.reduced(cell.getWidth() * 0.30f, cell.getHeight() * 0.30f), 2.0f);
            }
        }
        else
        {
            g.setColour(lens);
            g.fillPath(shape);
        }
        MuLookAndFeel::drawRecessedScreen(g, cell);

        if (isBeat && row == 0)   // step numbers on the beat-group starts (1/5/9/13), on the first row only
        {
            g.setFont(juce::Font(juce::FontOptions(mu_ui::sf(9.0f))));
            MuLookAndFeel::drawEngravedText(g, mu_ui::cachedIntLabel(s + 1), cell.reduced(2.0f).toNearestInt(),
                                            juce::Justification::topLeft, MuLookAndFeel::colour(MuLookAndFeel::labelText), false);
        }
    }

    if (isRumble) g.endTransparencyLayer();

    // The selected layer's row is outlined in its colour.
    if (row == selected)
    {
        g.setColour(col);
        g.drawRoundedRectangle(area.toFloat().reduced(0.5f), 3.0f, 1.5f);
    }
}

void StepRows::mouseDown(const juce::MouseEvent& e)
{
    const int rh = rowHeight();
    if (rh <= 0) return;
    const int row = e.getPosition().y / rh;
    if (row < 0 || row >= proc.getNumLayers()) return;
    if (onRowClicked) onRowClicked(row);

    // A Rumble row is not editable; a click on a cell of any other row edits it.
    int r = 0, s = 0;
    if (! cellAt(e.getPosition(), r, s) || proc.engineOf(r) == EngineType::Rumble) return;

    // Right-click toggles accent, but only on an ON step (accent is invisible/inert
    // otherwise); on an OFF step it turns the step on so the gesture is never a no-op.
    auto& pattern = proc.pattern();
    if (e.mods.isRightButtonDown())
    {
        if (pattern.isOn(r, s)) pattern.setAccent(r, s, ! pattern.isAccent(r, s));
        else                    { pattern.setOn(r, s, true); pattern.setAccent(r, s, true); }
    }
    else
    {
        pattern.toggle(r, s);   // left-click → on/off
    }
    repaint(rowArea(r));
}

void StepRows::timerCallback()
{
    // A structural change or an engine change (host automation, a preset) redraws every row.
    if (const auto sig = signature(); sig != shownSignature)
    {
        shownSignature = sig;
        repaint();
    }

    const int step = proc.isInternalPlaying()
                        ? GrooveSequencer::currentStep(proc.getInternalBeatPos()) : -1;
    if (step == playheadStep) return;

    // Repaint only the cells the playhead left and entered, not the whole grid.
    const float cellW = (getWidth() - mu_ui::s(kLabelW)) / (float) StepPattern::kNumSteps;
    for (int s : { playheadStep, step })
        if (s >= 0)
            repaint(juce::Rectangle<float>((float) mu_ui::s(kLabelW) + s * cellW, 0.0f, cellW, (float) getHeight())
                        .getSmallestIntegerContainer());
    playheadStep = step;
}

} // namespace mu_on
