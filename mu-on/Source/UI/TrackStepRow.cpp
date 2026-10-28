#include "TrackStepRow.h"
#include "Audio/MusicalScales.h"   // mu-core: note names, scale membership
#include "Audio/ChordVoicing.h"      // a Chord step's name
#include "Sequencer/StepClock.h"   // currentStep
#include "Sequencer/Track.h"       // the row the cells edit
#include "ValueFormat.h"           // mu-core: the octave text for the status bar
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
    {
        const float hz = tune->load();
        key.baseMidi = engine == EngineType::Bass && isBassTuneRoot(hz)
                         ? bassRootMidi(key.root)   // the Tune at its default: the key's root note
                         : juce::roundToInt(mu_audio::snapMidiToScale(mu_audio::freqToMidi(hz), key.root, key.scale));
    }
    return key;
}

// What the row shows beyond the lamps, from the track's engine, tune and key and (while playing) this pass's draws.
TrackStepRow::CellOptions TrackStepRow::optionsFor(TrackHost& host, int slot, int firstStep)
{
    CellOptions o;
    o.firstStep = firstStep;
    const auto engine = host.getTrackEngine(slot);
    auto& track = host.getTrack(slot);
    o.ties = engine == EngineType::Bass;
    o.hatIcons = engine == EngineType::Hat;
    if (engine == EngineType::Bass)
    {
        const int base = tuneKey(host, slot, engine).baseMidi;
        o.noteName = [base, &track](int step) { return mu_audio::midiNoteName(base + track.row.stepSemitones(step)); };
    }
    if (engine == EngineType::Chord)
    {
        const auto key = tuneKey(host, slot, engine);
        o.noteName = [key, &track](int step)
        {
            return chordRootName(key.root, key.scale, track.row.stepNote(step)) + "\n" + kChords[(size_t) track.row.stepChord(step)].shortName;
        };
    }
    if (engine != EngineType::Rumble && host.isTrackTransportPlaying())
    {
        const long long pass = StepClock::passOfBeat(host.getTrackTransportBeat(), track.row.numSteps());
        for (int s = 0; s < StepRow::kNumSteps; ++s)
            if (track.row.isOn(firstStep + s) && ! track.stepPlays(firstStep + s, pass)) o.dimMask = (std::uint16_t) (o.dimMask | (1u << s));
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
    for (int pos = 0; pos < StepRow::kNumSteps; ++pos)
    {
        const int s = opts.firstStep + pos;                       // the row step this cell shows
        const auto cell = cellRect(cells, pos);
        const bool beyond = s >= row.numSteps();                  // past the row's length: a blank, unlit lens
        const bool on     = showSteps && ! beyond && row.isOn(s);
        const bool isBeat = (s % 4) == 0;
        float lit = on ? L.lampOn : (isBeat ? L.lampBeat : L.lampOff);
        if (on && s == playheadStep) lit = juce::jmin(1.0f, lit + L.lampPlayhead);

        const bool ghost = on && row.isGhost(s);
        // A ghost step is drawn as an outline in its square: the lens stays dark, a ring in the layer colour marks it.
        const auto lens = MuLookAndFeel::lampColour(colour, ghost ? L.lampOff : lit);
        juce::Path shape;
        shape.addRoundedRectangle(cell, 3.0f);
        if (on && ! ghost)
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
        if (ghost)
        {
            // The ring's thickness is the Ghost level's share of half the square: nothing at 0 %, the whole square (no longer a
            // ghost) at 100 %.
            const float thick = row.ghostLevel() * 0.5f * juce::jmin(cell.getWidth(), cell.getHeight());
            if (thick > 0.3f)
            {
                g.setColour(colour.brighter(0.3f).withAlpha(0.95f));
                g.drawRoundedRectangle(cell.reduced(thick * 0.5f), juce::jmax(1.0f, 3.0f - thick * 0.5f), thick);
            }
        }
        if (opts.ties && showSteps && pos > 0 && ! beyond && row.tiedFrom(s))   // tied to the step before: a bar joins the two lamps
        {
            const auto prev = cellRect(cells, pos - 1);
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
                g.drawFittedText(opts.noteName(s), cell.toNearestInt().reduced(1), juce::Justification::centred, 2, 1.0f);
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

        if (beyond)   // a step past the row's length is not part of the pattern: shaded out
        {
            g.setColour(MuLookAndFeel::colour(MuLookAndFeel::panelBackground).darker(0.6f).withAlpha(0.92f));
            g.fillRoundedRectangle(cell, 3.0f);
        }
        else if ((opts.dimMask >> pos) & 1u)   // a step this pass will skip (its loop ratio or its probability): dimmed
        {
            g.setColour(juce::Colours::black.withAlpha(0.6f));
            g.fillRoundedRectangle(cell, 3.0f);
        }
    }
}

int TrackStepRow::stepAt(juce::Rectangle<int> cells, juce::Point<int> p, int firstStep)
{
    return firstStep + juce::jlimit(0, StepRow::kNumSteps - 1, (int) ((p.x - cells.getX()) / (cells.getWidth() / (float) StepRow::kNumSteps)));
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

bool TrackStepRow::clickStep(StepRow& row, int step, juce::Rectangle<int> cells, juce::Point<int> p, bool rightClick, int firstStep)
{
    if (step >= row.numSteps()) return false;   // past the row's length: nothing to edit
    const auto cell = cellRect(cells, step - firstStep);
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

bool TrackStepRow::dragStepValue(TrackHost& host, int slot, int step, StepDrag& d, const juce::MouseEvent& e,
                                 juce::String& statusName, juce::String& statusValue)
{
    auto& row = host.getTrack(slot).row;
    const auto engine = host.getTrackEngine(slot);
    const int dy = e.getDistanceFromDragStartY(), dx = e.getDistanceFromDragStartX();
    if (! d.active)
    {
        d.startSemis = row.stepSemitones(step);   // the step as it was, until the drag takes hold
        d.startOct = row.stepOctave(step);
        if (std::abs(dy) < kPitchDragStartPx || std::abs(dy) <= std::abs(dx)) return false;
        const bool pitched = engine == EngineType::Bass || engine == EngineType::Kick || engine == EngineType::Chord;
        if (e.mods.isAltDown())                         d.mode = DragMode::Accent;
        else if (e.mods.isShiftDown() && pitched)       d.mode = DragMode::Octave;
        else if (engine == EngineType::Bass)            d.mode = DragMode::Note;
        else if (engine == EngineType::Hat)             d.mode = DragMode::Voice;
        else return false;
        d.active = true;
    }

    statusName = "Step " + juce::String(step + 1);
    switch (d.mode)
    {
        case DragMode::Note:
        {
            const auto key = tuneKey(host, slot, EngineType::Bass);
            const int degrees = juce::roundToInt(-dy / (float) kPixelsPerDegree);   // up is a higher note
            const int midi = degrees == 0 ? key.baseMidi + d.startSemis
                                          : mu_audio::moveMidiInScale(key.baseMidi + d.startSemis, degrees, key.root, key.scale);
            row.setStepSemitones(step, midi - key.baseMidi);
            statusName += " Note";
            statusValue = mu_audio::midiNoteName(key.baseMidi + row.stepSemitones(step));
            break;
        }
        case DragMode::Octave:
            row.setStepOctave(step, d.startOct + juce::roundToInt(-dy / (float) kPixelsPerOctave));
            statusName += " Octave";
            statusValue = mu_fmt::unitText(mu_fmt::Unit::Octaves, row.stepOctave(step));
            break;
        case DragMode::Accent:
        {
            // Up accents; down ghosts a drum, percussion or Chord step (a Bass step just loses its accent); near the press it is normal.
            const bool ghostable = engine == EngineType::Kick || engine == EngineType::Hat || engine == EngineType::Snare || engine == EngineType::FmPerc || engine == EngineType::Chord;
            const bool up = dy <= -kPitchDragStartPx, down = dy >= kPitchDragStartPx;
            row.setAccent(step, up);
            if (ghostable) row.setGhost(step, down);
            statusName += ghostable ? " Dynamics" : " Accent";
            statusValue = row.isAccent(step) ? "Accent" : row.isGhost(step) ? "Ghost" : ghostable ? "Normal" : "Off";
            break;
        }
        case DragMode::Voice:
            row.setOpen(step, dy < 0);
            statusName += " Hat";
            statusValue = row.isOpen(step) ? "Open" : "Closed";
            break;
        case DragMode::None: return false;
    }
    return true;
}

void TrackStepRow::paint(juce::Graphics& g)
{
    const bool isRumble = host.getTrackEngine(slot) == EngineType::Rumble;
    const auto colour = MuLookAndFeel::layerPalette[
        (size_t) juce::jlimit(0, MuLookAndFeel::kLayerPaletteSize - 1, host.getTrackColourIndex(slot))];

    const juce::Graphics::ScopedSaveState state(g);
    if (isRumble) g.beginTransparencyLayer(0.45f);
    paintCells(g, getLocalBounds(), host.getTrack(slot).row, ! isRumble, colour, playheadStep, true, selectedStep, optionsFor(host, slot, firstStep));
    if (isRumble) g.endTransparencyLayer();
}

void TrackStepRow::mouseDown(const juce::MouseEvent& e)
{
    // A Rumble row is not editable.
    if (host.getTrackEngine(slot) == EngineType::Rumble || ! getLocalBounds().contains(e.getPosition())) return;
    const int step = stepAt(getLocalBounds(), e.getPosition(), firstStep);
    dragStartedOn = host.getTrack(slot).row.isOn(step);
    stepDrag = {};
    const bool selects = clickStep(host.getTrack(slot).row, step, getLocalBounds(), e.getPosition(), e.mods.isPopupMenu(), firstStep);
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
    if (dragStep < 0 || host.getTrackEngine(slot) == EngineType::Rumble) return;
    juce::String name, value;
    if (dragStepValue(host, slot, dragStep, stepDrag, e, name, value))
    {
        if (onStatusUpdate) onStatusUpdate(name, value);
        if (onStepSelected) onStepSelected(dragStep);   // the Step panel shows the new value
        repaint();
        return;
    }
    if (host.getTrackEngine(slot) != EngineType::Bass) return;   // only a Bass sequence ties notes
    const int step = stepAt(getLocalBounds(), e.getPosition(), firstStep);
    if (step == dragStep || step >= host.getTrack(slot).row.numSteps()) return;
    dragTie(host.getTrack(slot).row, dragStep, step, dragStartedOn);
    dragStep = step;
    dragStartedOn = true;   // the steps are on now: the next pair crossed in this drag toggles its tie the same way
    repaint();
}

void TrackStepRow::mouseMove(const juce::MouseEvent& e)
{
    if (! onStatusUpdate || host.getTrackEngine(slot) == EngineType::Rumble) return;
    const int step = stepAt(getLocalBounds(), e.getPosition(), firstStep);
    if (step == hoverStep) return;
    hoverStep = step;
    const auto& row = host.getTrack(slot).row;
    onStatusUpdate("Step", "Step " + juce::String(step + 1) + (row.isOn(step) ? (row.isOpen(step) ? (row.isAccent(step) ? " open, accent" : row.isGhost(step) ? " open, ghost" : " open") : (row.isAccent(step) ? " on, accent" : row.isGhost(step) ? " on, ghost" : " on")) : " off"));
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

    const int length = host.getTrack(slot).row.numSteps();
    const int step = host.isTrackTransportPlaying()
                        ? StepClock::currentStep(host.getTrackTransportBeat(), length) : -1;
    // A new pass draws new skips: redraw the dimmed steps.
    const long long pass = step >= 0 ? StepClock::passOfBeat(host.getTrackTransportBeat(), length) : -1;
    if (pass != shownPass) { shownPass = pass; repaint(); }
    if (step == playheadStep) return;
    playheadStep = step;
    repaint();
}

} // namespace mu_on
