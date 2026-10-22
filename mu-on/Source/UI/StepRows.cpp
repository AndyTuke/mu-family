#include "StepRows.h"
#include "UI/TrackStepRow.h"       // the one drawing of a step row
#include "Sequencer/StepClock.h"   // currentStep
#include "Sequencer/Track.h"             // the rows the cells edit
#include "UI/Components/MuLookAndFeel.h"

namespace mu_on
{

StepRows::StepRows(TrackHost& trackHost)
    : host(trackHost)
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
    return MuLookAndFeel::layerPalette[
        (size_t) juce::jlimit(0, MuLookAndFeel::kLayerPaletteSize - 1, host.getTrackColourIndex(row))];
}

bool StepRows::cellAt(juce::Point<int> p, int& row, int& step) const
{
    const int rh = rowHeight();
    if (rh <= 0) return false;
    row = p.y / rh;
    if (row < 0 || row >= host.getTrackCount()) return false;
    const auto cells = cellsArea(row);
    if (! cells.contains(p)) return false;
    step = TrackStepRow::stepAt(cells, p);
    return true;
}

juce::uint64 StepRows::signature() const
{
    // Count, then each live row's colour and engine, folded into one number.
    juce::uint64 h = (juce::uint64) host.getTrackCount();
    for (int i = 0; i < host.getTrackCount(); ++i)
        h = h * 131u + (juce::uint64) (host.getTrackColourIndex(i) * 8 + toIndex(host.getTrackEngine(i)));
    return h;
}

void StepRows::paint(juce::Graphics& g)
{
    for (int row = 0; row < host.getTrackCount(); ++row)
        paintRow(g, row);
}

// One row: the name gutter, then the 16 steps as lamps behind dark lenses — on steps lit, accents
// brighter at the centre, beat starts marked by a hint of colour; empty steps stay dark even under the
// playhead (family lamp rule). A Rumble row shows the same lenses unlit and the whole row dimmed.
void StepRows::paintRow(juce::Graphics& g, int row)
{
    const auto col = layerColour(row);
    const bool isRumble = host.getTrackEngine(row) == EngineType::Rumble;
    const auto area = rowArea(row);

    const juce::Graphics::ScopedSaveState state(g);
    if (isRumble) g.beginTransparencyLayer(0.45f);

    // Name gutter: "1  Kick" in the layer's colour.
    g.setFont(juce::Font(juce::FontOptions(mu_ui::sf(11.0f), juce::Font::bold)));
    const auto label = juce::String(row + 1) + "  " + host.getTrackName(row);
    MuLookAndFeel::drawEngravedText(g, label, area.withWidth(mu_ui::s(kLabelW)).reduced(mu_ui::s(2), 0),
                                    juce::Justification::centredLeft, col, false);

    // The 16 steps; the step numbers on the first row only.
    TrackStepRow::paintCells(g, cellsArea(row), host.getTrack(row).row, ! isRumble, col, playheadStep, row == 0,
                             row == selected ? selectedStep : -1);

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
    if (row < 0 || row >= host.getTrackCount()) return;
    if (onRowClicked) onRowClicked(row);

    // A Rumble row is not editable; a click on a cell of any other row edits it.
    int r = 0, s = 0;
    if (! cellAt(e.getPosition(), r, s) || host.getTrackEngine(r) == EngineType::Rumble) return;

    // Left toggles the step (its corner button the accent); right selects it for the Step panel.
    if (TrackStepRow::clickStep(host.getTrack(r).row, s, cellsArea(r), e.getPosition(), e.mods.isPopupMenu()))
    {
        selectedStep = s;
        if (onStepSelected) onStepSelected(r, s);
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

    const int step = host.isTrackTransportPlaying()
                        ? StepClock::currentStep(host.getTrackTransportBeat()) : -1;
    if (step == playheadStep) return;

    // Repaint only the cells the playhead left and entered, not the whole grid.
    const float cellW = (getWidth() - mu_ui::s(kLabelW)) / (float) StepRow::kNumSteps;
    for (int s : { playheadStep, step })
        if (s >= 0)
            repaint(juce::Rectangle<float>((float) mu_ui::s(kLabelW) + s * cellW, 0.0f, cellW, (float) getHeight())
                        .getSmallestIntegerContainer());
    playheadStep = step;
}

} // namespace mu_on
