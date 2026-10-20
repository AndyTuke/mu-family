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
        const juce::Rectangle<float> cell((float) cells.getX() + s * cellW + 1.5f, (float) cells.getY() + 1.5f,
                                          cellW - 3.0f, (float) cells.getHeight() - 3.0f);
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

// Right-click toggles accent, but only on an ON step (accent is invisible/inert otherwise); on an OFF step it
// turns the step on so the gesture is never a no-op. Left-click is on/off.
void TrackStepRow::editStep(StepRow& row, int step, bool accentGesture)
{
    if (accentGesture)
    {
        if (row.isOn(step)) row.setAccent(step, ! row.isAccent(step));
        else                { row.setOn(step, true); row.setAccent(step, true); }
    }
    else
        row.setOn(step, ! row.isOn(step));
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
    editStep(host.getTrack(slot).row, stepAt(getLocalBounds(), e.getPosition()), e.mods.isRightButtonDown());
    repaint();
}

void TrackStepRow::mouseMove(const juce::MouseEvent& e)
{
    if (! onStatusUpdate || host.getTrackEngine(slot) == EngineType::Rumble) return;
    const int step = stepAt(getLocalBounds(), e.getPosition());
    if (step == hoverStep) return;
    hoverStep = step;
    const auto& row = host.getTrack(slot).row;
    onStatusUpdate("Step", "Step " + juce::String(step + 1) + (row.isOn(step) ? (row.isAccent(step) ? " on, accent" : " on") : " off"));
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
