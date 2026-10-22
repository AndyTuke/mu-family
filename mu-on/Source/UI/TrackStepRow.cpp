#include "TrackStepRow.h"
#include "Audio/MusicalScales.h"   // mu-core: note names, scale membership
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

TrackStepRow::TuneKey TrackStepRow::tuneKey(TrackHost& host, int slot, EngineType engine)
{
    TuneKey key;
    auto& apvts = host.getTrackParameters();
    if (auto* r = apvts.getRawParameterValue(kRootParamId))  key.root  = juce::jlimit(0, 11, juce::roundToInt(r->load()));
    if (auto* sc = apvts.getRawParameterValue(kScaleParamId)) key.scale = mu_audio::clampScaleIndex(juce::roundToInt(sc->load()));
    if (auto* tune = apvts.getRawParameterValue(engineParamId(slot, engine, "tune")))
        key.baseMidi = juce::roundToInt(mu_audio::snapMidiToScale(mu_audio::freqToMidi(tune->load()), key.root, key.scale));
    return key;
}

// What the row shows beyond the lamps, from the track's engine, tune and key and (while playing) this pass's draws.
TrackStepRow::CellOptions TrackStepRow::optionsFor(TrackHost& host, int slot)
{
    CellOptions o;
    const auto engine = host.getTrackEngine(slot);
    auto& track = host.getTrack(slot);
    o.ties = engine == EngineType::Bass;
    o.hatIcons = engine == EngineType::Hat;
    if (engine == EngineType::Bass)
    {
        const int base = tuneKey(host, slot, engine).baseMidi;
        o.noteName = [base, &track](int step) { return mu_audio::midiNoteName(base + track.row.stepSemitones(step)); };
    }
    if (engine != EngineType::Rumble && host.isTrackTransportPlaying())
    {
        const long long pass = StepClock::passOfBeat(host.getTrackTransportBeat());
        for (int s = 0; s < StepRow::kNumSteps; ++s)
            if (track.row.isOn(s) && ! track.stepPlays(s, pass)) o.dimMask = (std::uint16_t) (o.dimMask | (1u << s));
    }
    return o;
}

// The 16 steps as lamps behind dark lenses: on steps lit, accents brighter at the centre, beat starts marked by a
// hint of colour; empty steps stay dark even under the playhead (family lamp rule). On a step that is on: the accent
// button top left, the delete cross top right, and in the middle a Bass's note or a Hat's open / closed icon.
void TrackStepRow::paintCells(juce::Graphics& g, juce::Rectangle<int> cells, const StepRow& row, bool showSteps,
                              juce::Colour colour, int playheadStep, bool beatNumbers, int selectedStep, const CellOptions& opts)
{
    const auto& L = MuLookAndFeel::lighting();
    const auto labelColour = MuLookAndFeel::colour(MuLookAndFeel::labelText);
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
        if (opts.ties && showSteps && row.tiedFrom(s))   // tied to the step before: a bar joins the two lamps
        {
            const auto prev = cellRect(cells, s - 1);
            const float h = cell.getHeight() * 0.45f;
            g.setColour(lens.brighter(L.lampHot * 2.0f));
            g.fillRect(juce::Rectangle<float>(prev.getRight() - 2.0f, cell.getCentreY() - h * 0.5f, cell.getX() - prev.getRight() + 4.0f, h));
        }
        if (s == selectedStep)   // the step the Step panel edits
        {
            g.setColour(juce::Colours::white.withAlpha(0.85f));
            g.drawRoundedRectangle(cell.reduced(0.5f), 3.0f, 1.5f);
        }

        if (on)
        {
            // The accent button, top left: lit when the step is accented.
            const auto btn = accentButtonRect(cell);
            const bool accented = row.isAccent(s);
            g.setColour(accented ? colour.brighter(0.4f) : labelColour.withAlpha(0.55f));
            if (accented) g.fillRoundedRectangle(btn, 2.0f);
            g.drawRoundedRectangle(btn, 2.0f, 1.0f);

            // The delete cross, top right.
            const auto x = deleteButtonRect(cell).reduced(1.5f);
            g.setColour(labelColour.withAlpha(0.7f));
            g.drawLine(x.getX(), x.getY(), x.getRight(), x.getBottom(), 1.2f);
            g.drawLine(x.getX(), x.getBottom(), x.getRight(), x.getY(), 1.2f);

            // The middle: a Bass step's note; a Hat step's icon (a ring for open, a dot for closed).
            if (opts.noteName)
            {
                g.setFont(juce::Font(juce::FontOptions(mu_ui::sf(10.0f), juce::Font::bold)));
                g.setColour(juce::Colours::white.withAlpha(0.92f));
                g.drawText(opts.noteName(s), cell.toNearestInt(), juce::Justification::centred, false);
            }
            if (opts.hatIcons)
            {
                const float d = juce::jmin(cell.getWidth(), cell.getHeight()) * 0.32f;
                const auto icon = juce::Rectangle<float>(d, d).withCentre(cell.getCentre());
                g.setColour(juce::Colours::white.withAlpha(0.92f));
                if (row.isOpen(s)) g.drawEllipse(icon, 1.6f);
                else               g.fillEllipse(icon.reduced(d * 0.2f));
            }
        }

        if (isBeat && beatNumbers)   // step numbers on the beat-group starts (1/5/9/13), bottom left
        {
            g.setFont(juce::Font(juce::FontOptions(mu_ui::sf(9.0f))));
            MuLookAndFeel::drawEngravedText(g, mu_ui::cachedIntLabel(s + 1), cell.reduced(2.0f).toNearestInt(),
                                            juce::Justification::bottomLeft, labelColour, false);
        }

        if ((opts.dimMask >> s) & 1u)   // a step this pass will skip (its loop ratio or its probability): dimmed
        {
            g.setColour(juce::Colours::black.withAlpha(0.6f));
            g.fillRoundedRectangle(cell, 3.0f);
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
    return { cell.getX() + 2.0f, cell.getY() + 2.0f, side, side };
}

juce::Rectangle<float> TrackStepRow::deleteButtonRect(juce::Rectangle<float> cell)
{
    const float side = juce::jmax(6.0f, juce::jmin(cell.getWidth(), cell.getHeight()) * 0.30f);
    return { cell.getRight() - side - 2.0f, cell.getY() + 2.0f, side, side };
}

bool TrackStepRow::clickStep(StepRow& row, int step, juce::Rectangle<int> cells, juce::Point<int> p, bool rightClick)
{
    const auto cell = cellRect(cells, step);
    if (! rightClick && row.isOn(step))
    {
        if (accentButtonRect(cell).expanded(2.0f).contains(p.toFloat())) { row.setAccent(step, ! row.isAccent(step)); return false; }
        if (deleteButtonRect(cell).expanded(2.0f).contains(p.toFloat())) { row.setOn(step, false); return false; }   // its other settings stay for when it is recreated
    }
    if (! rightClick && ! row.isOn(step)) row.setOn(step, true);   // an empty step is created, as it was
    return true;
}

void TrackStepRow::dragTie(StepRow& row, int from, int to, bool startedOn)
{
    if (std::abs(from - to) != 1) return;
    const int later = juce::jmax(from, to);
    const bool untie = startedOn && row.isOn(to) && row.tiedFrom(later);
    row.setOn(from, true);
    row.setOn(to, true);
    row.setTied(later, ! untie);
}

void TrackStepRow::paint(juce::Graphics& g)
{
    const bool isRumble = host.getTrackEngine(slot) == EngineType::Rumble;
    const auto colour = MuLookAndFeel::layerPalette[
        (size_t) juce::jlimit(0, MuLookAndFeel::kLayerPaletteSize - 1, host.getTrackColourIndex(slot))];

    const juce::Graphics::ScopedSaveState state(g);
    if (isRumble) g.beginTransparencyLayer(0.45f);
    paintCells(g, getLocalBounds(), host.getTrack(slot).row, ! isRumble, colour, playheadStep, true, selectedStep, optionsFor(host, slot));
    if (isRumble) g.endTransparencyLayer();
}

void TrackStepRow::mouseDown(const juce::MouseEvent& e)
{
    // A Rumble row is not editable.
    if (host.getTrackEngine(slot) == EngineType::Rumble || ! getLocalBounds().contains(e.getPosition())) return;
    const int step = stepAt(getLocalBounds(), e.getPosition());
    dragStartedOn = host.getTrack(slot).row.isOn(step);
    const bool selects = clickStep(host.getTrack(slot).row, step, getLocalBounds(), e.getPosition(), e.mods.isPopupMenu());
    dragStep = selects && ! e.mods.isPopupMenu() ? step : -1;
    if (selects)
    {
        selectedStep = step;
        if (onStepSelected) onStepSelected(step);
    }
    repaint();
}

void TrackStepRow::mouseDrag(const juce::MouseEvent& e)
{
    if (dragStep < 0 || host.getTrackEngine(slot) != EngineType::Bass) return;   // only a Bass sequence ties notes
    const int step = stepAt(getLocalBounds(), e.getPosition());
    if (step == dragStep) return;
    dragTie(host.getTrack(slot).row, dragStep, step, dragStartedOn);
    dragStep = step;
    dragStartedOn = true;   // the steps are on now: the next pair crossed in this drag toggles its tie the same way
    repaint();
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
    // A new pass draws new skips: redraw the dimmed steps.
    const long long pass = step >= 0 ? StepClock::passOfBeat(host.getTrackTransportBeat()) : -1;
    if (pass != shownPass) { shownPass = pass; repaint(); }
    if (step == playheadStep) return;
    playheadStep = step;
    repaint();
}

} // namespace mu_on
