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
    playheadSteps.fill(-1);
    shownPasses.fill(-2);
    pager.pageCount = [this] { return numPages(); };
    pager.lengthRow = [this]() -> StepRow* { return selected >= 0 && selected < host.getTrackCount() ? &host.getTrack(selected).row : nullptr; };
    pager.onChanged = [this] { repaint(); if (onLengthChanged) onLengthChanged(); };
    addAndMakeVisible(pager);
    refreshControls();
    startTimerHz(mu_ui::kUiRefreshHz);   // playhead
}

void StepRows::setSelected(int idx)
{
    selected = idx;
    refreshControls();
    repaint();
}

// The pages the longest live row needs: every row shows the same page.
int StepRows::numPages() const
{
    int pages = 1;
    for (int i = 0; i < host.getTrackCount(); ++i)
        pages = juce::jmax(pages, host.getTrack(i).row.numPages());
    return pages;
}

// The control strip under the rows, lined up with the step cells.
void StepRows::resized()
{
    pager.setBounds(getLocalBounds().removeFromBottom(mu_ui::s(kControlsH)).withTrimmedLeft(mu_ui::s(kLabelW)));
}

int StepRows::rowHeight() const noexcept
{
    return (getHeight() - mu_ui::s(kControlsH)) / mu_limits::kMaxLayers;
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
    step = TrackStepRow::stepAt(cells, p, pager.page() * StepRow::kNumSteps);
    return true;
}

juce::uint64 StepRows::signature() const
{
    // Count, then each live row's colour and engine, folded into one number.
    juce::uint64 h = (juce::uint64) host.getTrackCount();
    for (int i = 0; i < host.getTrackCount(); ++i)
        h = h * 131u + (juce::uint64) (host.getTrackColourIndex(i) * 8 + toIndex(host.getTrackEngine(i)) + 4096 * host.getTrack(i).row.numSteps());
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

    // The page's 16 steps; the step numbers on the first row only.
    TrackStepRow::paintCells(g, cellsArea(row), host.getTrack(row).row, ! isRumble, col, playheadSteps[(size_t) row], row == 0,
                             row == selected ? selectedStep : -1, TrackStepRow::optionsFor(host, row, pager.page() * StepRow::kNumSteps));

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

    if (s >= host.getTrack(r).row.numSteps()) return;   // past the row's length: nothing to edit
    dragStartedOn = host.getTrack(r).row.isOn(s);
    stepDrag = {};

    // A click selects the step for the Step panel (creating it if empty); its corner buttons set the accent or delete it.
    const bool selects = TrackStepRow::clickStep(host.getTrack(r).row, s, cellsArea(r), e.getPosition(), e.mods.isPopupMenu(),
                                                 pager.page() * StepRow::kNumSteps);
    dragRow = selects && ! e.mods.isPopupMenu() ? r : -1;
    dragStep = s;
    if (selects)
    {
        selectedStep = s;
        if (onStepSelected) onStepSelected(r, s);
    }
    repaint(rowArea(r));
}

void StepRows::mouseDrag(const juce::MouseEvent& e)
{
    int r = 0, s = 0;
    if (dragRow >= 0 && host.getTrackEngine(dragRow) != EngineType::Rumble)
    {
        juce::String name, value;
        if (TrackStepRow::dragStepValue(host, dragRow, dragStep, stepDrag, e, name, value))
        {
            if (onStepSelected) onStepSelected(dragRow, dragStep);   // the Step panel shows the new value
            repaint(rowArea(dragRow));
            return;
        }
    }
    if (dragRow < 0 || host.getTrackEngine(dragRow) != EngineType::Bass || ! cellAt(e.getPosition(), r, s) || r != dragRow || s == dragStep
        || s >= host.getTrack(r).row.numSteps()) return;
    TrackStepRow::dragTie(host.getTrack(r).row, dragStep, s, dragStartedOn);
    dragStep = s;
    dragStartedOn = true;
    repaint(rowArea(r));
}

void StepRows::timerCallback()
{
    // A structural change or an engine change (host automation, a preset) redraws every row.
    if (const auto sig = signature(); sig != shownSignature)
    {
        shownSignature = sig;
        refreshControls();
        repaint();
    }

    // Each row's playhead step (rows have their own lengths). A moved playhead, or a new pass drawing new skips, redraws the rows.
    bool changed = false;
    const bool playing = host.isTrackTransportPlaying();
    const double beat = playing ? host.getTrackTransportBeat() : 0.0;
    for (int i = 0; i < mu_limits::kMaxLayers; ++i)
    {
        int step = -1;
        long long pass = -1;
        if (playing && i < host.getTrackCount())
        {
            const int length = host.getTrack(i).row.numSteps();
            step = StepClock::currentStep(beat, length);
            pass = StepClock::passOfBeat(beat, length);
        }
        if (pass != shownPasses[(size_t) i]) { shownPasses[(size_t) i] = pass; changed = true; }
        if (step != playheadSteps[(size_t) i]) { playheadSteps[(size_t) i] = step; changed = true; }
    }
    if (changed) repaint(getLocalBounds().withTrimmedBottom(mu_ui::s(kControlsH)));
}

} // namespace mu_on
