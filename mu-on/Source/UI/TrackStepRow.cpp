#include "TrackStepRow.h"
#include "Sequencer/StepClock.h"   // currentStep
#include "Sequencer/Track.h"       // the row the cells edit
#include "UI/Components/MuLookAndFeel.h"

namespace mu_on
{

TrackStepRow::TrackStepRow(TrackHost& trackHost)
    : host(trackHost)
{
    startTimerHz(mu_ui::kUiRefreshHz);   // playhead
}

void TrackStepRow::setLayer(int layer)
{
    slot = juce::jlimit(0, mu_limits::kMaxLayers - 1, layer);
    shownSignature = 0;
    playheadStep = -1;
    repaint();
}

// The 16 steps as lamps behind dark lenses: on steps lit, accents brighter at the centre, beat starts marked by a
// hint of colour; empty steps stay dark even under the playhead (family lamp rule).
void TrackStepRow::paintCells(juce::Graphics& g, juce::Rectangle<int> cells, const StepRow& row, bool showSteps,
                              juce::Colour colour, int playheadStep, bool beatNumbers)
{
    const auto& L = MuLookAndFeel::lighting();
    const float cellW = cells.getWidth() / (float) StepRow::kNumSteps;
    for (int s = 0; s < StepRow::kNumSteps; ++s)
    {
        const auto cell = cellRect(cells, s);
        const bool on     = showSteps && row.isOn(s);
        const bool isBeat = (s % 4) == 0;
        float lit = on ? L.lampOn : (isBeat ? L.lampBeat : L.lampOff);
        if (on && s == playheadStep) lit = juce::jmin(1.0f, lit + L.lampPlayhead);

        const auto lens = MuLookAndFeel::lampColour(colour, lit);
        juce::Path shape;
        shape.addRoundedRectangle(cell, 3.0f);
        if (on)
        {
            MuLookAndFeel::drawLamp(g, shape, {}, lens, cell.getCentre(), juce::jmax(cell.getWidth(), cell.getHeight()) * 0.6f);
            if (row.isOpen(s))   // open hat: a bright ring round the lamp
            {
                g.setColour(lens.brighter(L.lampHot * 2.0f));
                g.drawRoundedRectangle(cell.reduced(1.0f), 3.0f, 1.5f);
            }
            if (row.isAccent(s))   // accent: a hotter centre
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

        // The accent button in the top-right corner: a small square, lit when the step is accented.
        if (showSteps)
        {
            const auto btn = accentButtonRect(cell);
            const bool accented = on && row.isAccent(s);
            g.setColour(accented ? colour.brighter(0.4f) : MuLookAndFeel::colour(MuLookAndFeel::labelText).withAlpha(on ? 0.55f : 0.25f));
            if (accented) g.fillRoundedRectangle(btn, 2.0f);
            g.drawRoundedRectangle(btn, 2.0f, 1.0f);
        }

        if (isBeat && beatNumbers)   // step numbers on the beat-group starts (1/5/9/13)
        {
            g.setFont(juce::Font(juce::FontOptions(mu_ui::sf(9.0f))));
            MuLookAndFeel::drawEngravedText(g, mu_ui::cachedIntLabel(s + 1), cell.reduced(2.0f).toNearestInt(),
                                            juce::Justification::topLeft, MuLookAndFeel::colour(MuLookAndFeel::labelText), false);
        }
    }
}

int TrackStepRow::stepAt(juce::Rectangle<int> cells, juce::Point<int> p)
{
    return juce::jlimit(0, StepRow::kNumSteps - 1, (int) ((p.x - cells.getX()) / (cells.getWidth() / (float) StepRow::kNumSteps)));
}

juce::Rectangle<float> TrackStepRow::cellRect(juce::Rectangle<int> cells, int step)
{
    const float cellW = cells.getWidth() / (float) StepRow::kNumSteps;
    return { (float) cells.getX() + step * cellW + 1.5f, (float) cells.getY() + 1.5f, cellW - 3.0f, (float) cells.getHeight() - 3.0f };
}

juce::Rectangle<float> TrackStepRow::accentButtonRect(juce::Rectangle<float> cell)
{
    const float side = juce::jmax(6.0f, juce::jmin(cell.getWidth(), cell.getHeight()) * 0.30f);
    return { cell.getRight() - side - 2.0f, cell.getY() + 2.0f, side, side };
}

void TrackStepRow::clickStep(StepRow& row, int step, juce::Rectangle<int> cells, juce::Point<int> p, bool rightClick,
                             bool hat, std::function<void()> changed)
{
    if (rightClick)
    {
        // Select the step: its states as a menu (accent stays on the corner button). A Hat step is Closed or Open.
        juce::PopupMenu menu;
        const bool on = row.isOn(step), open = row.isOpen(step);
        menu.addItem(1, "Off", true, ! on);
        if (hat) { menu.addItem(2, "Closed", true, on && ! open); menu.addItem(3, "Open", true, on && open); }
        else     menu.addItem(2, "On", true, on);
        menu.showMenuAsync(juce::PopupMenu::Options(), [&row, step, changed](int id)
        {
            if (id == 0) return;
            row.setOn(step, id != 1);
            row.setOpen(step, id == 3);
            if (id == 1) row.setAccent(step, false);
            if (changed) changed();
        });
        return;
    }

    if (accentButtonRect(cellRect(cells, step)).expanded(2.0f).contains(p.toFloat()))
    {
        // Accent on or off; on an off step it turns the step on accented, so the click is never a no-op.
        if (row.isOn(step)) row.setAccent(step, ! row.isAccent(step));
        else                { row.setOn(step, true); row.setAccent(step, true); }
    }
    else
        row.setOn(step, ! row.isOn(step));
    if (changed) changed();
}

void TrackStepRow::paint(juce::Graphics& g)
{
    const bool isRumble = host.getTrackEngine(slot) == EngineType::Rumble;
    const auto colour = MuLookAndFeel::layerPalette[
        (size_t) juce::jlimit(0, MuLookAndFeel::kLayerPaletteSize - 1, host.getTrackColourIndex(slot))];

    const juce::Graphics::ScopedSaveState state(g);
    if (isRumble) g.beginTransparencyLayer(0.45f);
    paintCells(g, getLocalBounds(), host.getTrack(slot).row, ! isRumble, colour, playheadStep, true);
    if (isRumble) g.endTransparencyLayer();
}

void TrackStepRow::mouseDown(const juce::MouseEvent& e)
{
    // A Rumble row is not editable.
    if (host.getTrackEngine(slot) == EngineType::Rumble || ! getLocalBounds().contains(e.getPosition())) return;
    clickStep(host.getTrack(slot).row, stepAt(getLocalBounds(), e.getPosition()), getLocalBounds(), e.getPosition(),
              e.mods.isPopupMenu(), host.getTrackEngine(slot) == EngineType::Hat,
              [safe = juce::Component::SafePointer<TrackStepRow>(this)] { if (safe != nullptr) safe->repaint(); });
}

void TrackStepRow::mouseMove(const juce::MouseEvent& e)
{
    if (! onStatusUpdate || host.getTrackEngine(slot) == EngineType::Rumble) return;
    const int step = stepAt(getLocalBounds(), e.getPosition());
    if (step == hoverStep) return;
    hoverStep = step;
    const auto& row = host.getTrack(slot).row;
    onStatusUpdate("Step", "Step " + juce::String(step + 1) + (row.isOn(step) ? (row.isOpen(step) ? (row.isAccent(step) ? " open, accent" : " open") : (row.isAccent(step) ? " on, accent" : " on")) : " off"));
}

void TrackStepRow::timerCallback()
{
    // The colour or the engine changed underneath us (a preset, host automation): redraw.
    const juce::uint64 sig = (juce::uint64) (host.getTrackColourIndex(slot) * 8 + toIndex(host.getTrackEngine(slot))) + 1u;
    if (sig != shownSignature)
    {
        shownSignature = sig;
        repaint();
    }

    const int step = host.isTrackTransportPlaying()
                        ? StepClock::currentStep(host.getTrackTransportBeat()) : -1;
    if (step == playheadStep) return;
    playheadStep = step;
    repaint();
}

} // namespace mu_on
