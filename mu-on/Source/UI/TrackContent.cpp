#include "TrackContent.h"
#include "Audio/SpinLock.h"   // mu-core: spin lock helpers

#include <limits>

namespace mu_on
{

namespace
{
    using Spec = mu_ui::ParamKnobGrid::Spec;

    // The selected engine's controls, bound to track `track` (in display order).
    std::vector<Spec> specsFor(int track, EngineType engine)
    {
        auto id = [&](const char* p) { return engineParamId(track, engine, p); };
        switch (engine)
        {
            case EngineType::Kick:
                return { { id("tune"), "Tune" }, { id("ptch"), "Pitch" }, { id("pdec"), "P.Dec" }, { id("adec"), "Decay" }, { id("drive"), "Drive" } };
            case EngineType::Bass:
                return { { id("wave"), "Wave" }, { id("tune"), "Tune" }, { id("sub"), "Sub" }, { id("cut"), "Cutoff" }, { id("res"), "Reso" },
                         { id("env"), "F.Env" }, { id("edec"), "E.Dec" }, { id("atk"), "Atk" }, { id("dec"), "Dec" }, { id("sus"), "Sus" }, { id("drive"), "Drive" } };
            case EngineType::Hat:
            case EngineType::Snare:
                return { { id("tune"), "Tune" }, { id("dec"), "Decay" } };
            // Rumble processes another track: input drive → 3 delay taps → reverb → filter.
            case EngineType::Rumble:
                return { { id("drive"), "Drive" }, { id("d1"), "1/16" }, { id("d2"), "2/16" }, { id("d3"), "3/16" },
                         { id("size"), "Rv Size" }, { id("revmix"), "Rv Mix" }, { id("revlp"), "Rv LP" }, { id("cut"), "Cutoff" }, { id("res"), "Reso" } };
        }
        return {};
    }

    void setParamPlain(juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float plain)
    {
        if (auto* p = apvts.getParameter(id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1(plain));
            p->endChangeGesture();
        }
    }
}

TrackContent::TrackContent(ProcessorBase& base, TrackHost& trackHost)
    : host(trackHost), grid(trackHost.getTrackParameters()), rows(trackHost), soloRow(trackHost)
{
    juce::ignoreUnused(base);
    for (int e = 0; e < kNumEngineTypes; ++e) engineSelect.addItem(engineName(engineFromIndex(e)), e + 1);
    engineSelect.onChange = [this](int id) { engineChosen(id); };
    sourceSelect.onChange = [this](int id) { sourceChosen(id); };

    for (auto* cap : { &engineCaption, &sourceCaption })
    {
        cap->setJustificationType(juce::Justification::centred);
        cap->setInterceptsMouseClicks(false, false);
        addAndMakeVisible(*cap);
    }
    addAndMakeVisible(engineSelect);
    addChildComponent(sourceSelect);
    sourceCaption.setVisible(false);
    addAndMakeVisible(grid);

    rows.onRowClicked = [this](int row) { if (onRowClicked) onRowClicked(row); };
    addAndMakeVisible(rows);
    soloRow.onStatusUpdate = [this](const juce::String& name, const juce::String& value) { if (onStatusUpdate) onStatusUpdate(name, value); };
    addChildComponent(soloRow);

    // The track's own Swing / Accent, shown in the Groove box (a Rumble track shows its envelope there instead);
    // setLayer attaches them to the shown track's rows.
    addAndMakeVisible(swingKnob);
    addAndMakeVisible(accentKnob);

    // The Rumble track's drawable bar-volume envelope, edited under the track's own lock.
    rumbleEnvEditor.setUnipolar(true);
    rumbleEnvEditor.onChange = [this](const std::vector<ControlSequence::CurvePoint>& pts)
    {
        auto& t = host.getTrack(currentLayer);
        mu_core::spinLock(t.rumbleEnvLock);
        t.rumbleEnv.curvePoints = pts;
        mu_core::spinUnlock(t.rumbleEnvLock);
    };
    addChildComponent(rumbleEnvEditor);

    // Its length: the same control as a modulator's Loop.
    rumbleLength.onChange = [this](NoteValue nv, NoteMod mod, int mult)
    {
        auto& t = host.getTrack(currentLayer);
        mu_core::spinLock(t.rumbleEnvLock);
        t.rumbleEnv.loopNoteValue  = nv;
        t.rumbleEnv.loopNoteMod    = mod;
        t.rumbleEnv.loopMultiplier = mult;
        mu_core::spinUnlock(t.rumbleEnvLock);
    };
    addChildComponent(rumbleLength);

    startTimerHz(mu_ui::kUiRefreshHz);   // engine changes underneath us, the envelope playhead
    setLayer(0);
}

juce::Colour TrackContent::layerColour() const
{
    return MuLookAndFeel::layerPalette[
        (size_t) (host.getTrackColourIndex(currentLayer) % MuLookAndFeel::kLayerPaletteSize)];
}

// Give every knob that has a row in the shown engine's modulation target table its live arc, driven by
// the track's matrix and the value the audio thread published for that target. Dropdowns have no
// target; the arc is cleared (NaN) while the transport is stopped.
void TrackContent::bindModulationArcs(std::vector<mu_ui::ParamKnobGrid::Spec>& specs) const
{
    static const float kNaN = std::numeric_limits<float>::quiet_NaN();
    int count = 0;
    const ModDestEntry* table = destsForEngine(shownEngine, count);
    const auto* matrix = &host.getTrack(currentLayer).modulationMatrix;
    const juce::String prefix = layerParamPrefix(currentLayer);
    const int layer = currentLayer;
    const EngineType engine = shownEngine;

    for (auto& spec : specs)
    {
        const int dest = mu_mod::indexOfParam(table, (std::size_t) count, spec.id.substring(prefix.length()).toStdString());
        if (dest < 0) continue;

        spec.modDestId = table[dest].id;
        spec.modMatrix = matrix;
        spec.modLive   = [this, layer, engine, dest]() -> float
                         { return host.isTrackTransportPlaying() ? host.getModLive(layer, engine, dest) : kNaN; };
    }
}

void TrackContent::setLayer(int idx)
{
    currentLayer = juce::jlimit(0, mu_limits::kMaxLayers - 1, idx);
    rebuild();
}

// Rebind every control to the shown track and its engine: the knob grid is rebuilt from the engine's
// specs (re-attaching to this track's parameters), and the Rumble-only controls follow the engine.
void TrackContent::rebuild()
{
    shownEngine = host.getTrackEngine(currentLayer);
    engineSelect.setSelectedId(toIndex(shownEngine) + 1, false);
    auto specs = specsFor(currentLayer, shownEngine);
    bindModulationArcs(specs);
    grid.setSpecs(specs);

    // The groove knobs follow the shown track's own rows (the old attachments go first).
    swingAtt.reset();
    accentAtt.reset();
    swingAtt  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(host.getTrackParameters(), layerParamId(currentLayer, "swing"),  swingKnob.getSlider());
    accentAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(host.getTrackParameters(), layerParamId(currentLayer, "accent"), accentKnob.getSlider());

    // A host that fixes the track's engine (engineLocked) hides the Engine selector and shows the track's one
    // row of steps; mu-On never locks, so its own look is unchanged.
    locked = host.getTrack(currentLayer).engineLocked;
    engineCaption.setVisible(! locked);
    engineSelect.setVisible(! locked);
    rows.setVisible(! locked);
    const bool rumble = shownEngine == EngineType::Rumble;
    soloRow.setVisible(locked && ! rumble);   // a locked Rumble has no steps: it follows its source
    soloRow.setLayer(currentLayer);

    sourceCaption.setVisible(rumble);
    sourceSelect.setVisible(rumble);
    swingKnob.setVisible(! rumble);
    accentKnob.setVisible(! rumble);
    rumbleLength.setVisible(rumble);
    rumbleEnvEditor.setVisible(rumble);

    if (rumble)
    {
        refreshSourceList();

        auto& t = host.getTrack(currentLayer);
        mu_core::spinLock(t.rumbleEnvLock);
        const auto points = t.rumbleEnv.curvePoints;
        const auto nv = t.rumbleEnv.loopNoteValue;
        const auto mod = t.rumbleEnv.loopNoteMod;
        const int mult = t.rumbleEnv.loopMultiplier;
        mu_core::spinUnlock(t.rumbleEnvLock);
        rumbleEnvEditor.setPoints(points);
        rumbleLength.setLength(nv, mod, mult);
    }

    rows.setSelected(currentLayer);
    resized();
    repaint();
}

// The Rumble's Source list: every other live layer, by number and name.
void TrackContent::refreshSourceList()
{
    sourceSelect.clear();
    juce::uint64 sig = (juce::uint64) host.getTrackCount();
    for (int i = 0; i < host.getTrackCount(); ++i)
    {
        sig = sig * 131u + (juce::uint64) toIndex(host.getTrackEngine(i));
        if (i != currentLayer && host.canBeRumbleSource(i))
            sourceSelect.addItem(juce::String(i + 1) + "  " + host.getTrackName(i), i + 1);
    }
    shownLayerSignature = sig;
    refreshSourceSelection();
}

void TrackContent::refreshSourceSelection()
{
    const int src = (int) std::lround(host.getTrackParameters().getRawParameterValue(layerParamId(currentLayer, "src"))->load());
    const bool usable = src >= 0 && src < host.getTrackCount() && src != currentLayer && host.canBeRumbleSource(src);
    sourceSelect.setSelectedId(usable ? src + 1 : 0, false);
}

void TrackContent::engineChosen(int id)
{
    host.selectTrackEngine(currentLayer, engineFromIndex(id - 1));
    rebuild();
    if (onEngineChanged) onEngineChanged();
}

void TrackContent::sourceChosen(int id)
{
    if (id > 0) setParamPlain(host.getTrackParameters(), layerParamId(currentLayer, "src"), (float) (id - 1));
}

void TrackContent::timerCallback()
{
    if (currentLayer >= host.getTrackCount()) return;   // between a delete and the editor reselecting

    // The engine changed underneath us (host automation, a preset): show the new one.
    if (host.getTrackEngine(currentLayer) != shownEngine)
    {
        rebuild();
        if (onEngineChanged) onEngineChanged();
        return;
    }
    if (shownEngine != EngineType::Rumble) return;

    // Keep the Source list current (layers added, deleted, re-engined) and the envelope's playhead moving.
    juce::uint64 sig = (juce::uint64) host.getTrackCount();
    for (int i = 0; i < host.getTrackCount(); ++i) sig = sig * 131u + (juce::uint64) toIndex(host.getTrackEngine(i));
    if (sig != shownLayerSignature) refreshSourceList();
    else                            refreshSourceSelection();

    const double beat = host.getTrackTransportBeat();
    // The loop length is read under the envelope lock (a state restore rewrites it); a busy lock skips this tick.
    auto& t = host.getTrack(currentLayer);
    if (! mu_core::spinLockFor(t.rumbleEnvLock, 1000)) return;
    const double loopBeats = t.rumbleEnv.getLoopLengthBeats();
    mu_core::spinUnlock(t.rumbleEnvLock);
    const double len = juce::jmax(1.0e-6, loopBeats);
    rumbleEnvEditor.setPlayheadPhase((float) (std::fmod(juce::jmax(0.0, beat), len) / len));
}

// Metal style: two panels edge to edge, content inset clear of the panels' corner screws, each section
// a raised box with its name plate in the band above it. The engine box is as wide as its controls need;
// the second panel takes the rest of the content band (the flex panel), its Groove box and steps box the
// same height.
void TrackContent::resized()
{
    using LF = MuLookAndFeel;
    using mu_ui::s;
    const int w = getWidth(), h = getHeight();
    const int padX   = s(LF::kScrewedPanelInset);   // content in from a panel's sides
    const int padY   = s(LF::kChannelInset);        //   … and from its top / bottom
    const int plateH = s(LF::kSectionPlateH);       // name plate band above each box
    const int clear  = s(LF::kSubPanelScrewClear);  // content in from a box's sides
    const int gap    = s(LF::kSpaceS);

    // Panel 1: the engine box. The dropdown(s) first, captioned on the knobs' label band and centred on
    // the knob band, then the knob grid.
    const bool rumble  = shownEngine == EngineType::Rumble;
    const int boxH     = s(mu_ui::ParamKnobGrid::kCellH + 2 * LF::kSpaceS);
    const int dropW    = s(MuLookAndFeel::kDropdownW);
    // The dropdowns: Engine (not on a locked track) then, for a Rumble, Source; the grid follows them.
    const int numDrops = (locked ? 0 : 1) + (rumble ? 1 : 0);
    const int leftW    = numDrops * dropW + juce::jmax(0, numDrops - 1) * gap;
    const int gridGap  = numDrops > 0 ? gap : 0;
    engineR    = { 0, 0, w, padY + plateH + boxH + padY };
    const int boxW = juce::jmin(w - 2 * padX, 2 * clear + leftW + gridGap + grid.getPreferredWidth());
    engineBoxR = { padX, padY + plateH, boxW, boxH };

    const auto inner = engineBoxR.reduced(clear, s(LF::kSpaceS));
    const int labelH = s(mu_ui::ParamKnobGrid::kLabelH);
    const int knobH  = s(LF::kKnobSize2H);
    const int bandH  = s(mu_ui::ParamKnobGrid::kCellH) - labelH;   // what the grid centres its controls in
    const juce::Font captionFont(juce::FontOptions{}.withHeight(mu_ui::sf(LF::kKnobLabelFont)));   // as the knob labels
    auto place = [&](juce::Label& caption, DropdownSelect& dropdown, int x)
    {
        caption.setFont(captionFont);
        caption.setBounds(x, inner.getY(), dropW, labelH);
        dropdown.setBounds(x, inner.getY() + labelH + (bandH - s(MuLookAndFeel::kDropdownH)) / 2, dropW, s(MuLookAndFeel::kDropdownH));
    };
    int dropX = inner.getX();
    if (! locked) { place(engineCaption, engineSelect, dropX); dropX += dropW + gap; }
    if (rumble)   place(sourceCaption, sourceSelect, dropX);
    grid.setBounds(inner.withTrimmedLeft(leftW + gridGap));

    // Panel 2 (flex): Groove | Steps, both the full height left under their plates.
    slotR = { 0, engineR.getBottom(), w, juce::jmax(0, h - engineR.getBottom()) };
    const int boxesY = slotR.getY() + padY + plateH;
    const int boxesH = juce::jmax(0, slotR.getHeight() - 2 * padY - plateH);
    // The left box of the second panel is as wide for Swing / Accent as for the Rumble envelope.
    // A locked Rumble has no step row, so its envelope takes the whole panel.
    const bool noSteps = locked && rumble;
    grooveBoxR = { padX, boxesY, noSteps ? juce::jmax(0, w - 2 * padX) : s(MuLookAndFeel::kGrooveBoxW), boxesH };
    const int stepsX = grooveBoxR.getRight() + s(LF::kVoiceDivW);
    stepsBoxR  = noSteps ? juce::Rectangle<int>() : juce::Rectangle<int>(stepsX, boxesY, juce::jmax(0, w - padX - stepsX), boxesH);
    const auto stepsInner = stepsBoxR.reduced(clear, s(LF::kSpaceS));
    rows.setBounds(stepsInner);
    // The lone row is as tall as one of the rows above is, top-aligned (the rest of the box stays blank metal).
    soloRow.setBounds(stepsInner.withHeight(stepsInner.getHeight() / mu_limits::kLayersPerPage));

    auto g = grooveBoxR.reduced(clear, s(LF::kSpaceS));
    if (rumble)
    {
        rumbleLength.setMetalStyle(LF::isMetal(*this));
        rumbleLength.setBounds(g.removeFromTop(s(NoteLengthControl::kHeight)).withWidth(s(NoteLengthControl::kWidth)));
        g.removeFromTop(gap);
        rumbleEnvEditor.setBounds(g);
    }
    else
    {
        // The pair centred in the box, top-aligned, each on the standard knob cell.
        const int knobW  = s(LF::kKnobSize2W);
        const int pairW  = 2 * knobW + gap;
        const int pairX  = g.getX() + (g.getWidth() - pairW) / 2;
        swingKnob.setBounds(pairX, g.getY(), knobW, knobH);
        accentKnob.setBounds(pairX + knobW + gap, g.getY(), knobW, knobH);
    }
}

void TrackContent::paint(juce::Graphics& g)
{
    g.fillAll(MuLookAndFeel::colour(MuLookAndFeel::panelBackground));
    if (! MuLookAndFeel::isMetal(*this)) return;

    // Metal style: both panels in the app colour, each section in a raised box named on a plate above it.
    const auto accent = MuLookAndFeel::appAccent(*this);
    for (auto r : { engineR, slotR })
        MuLookAndFeel::drawAccentPanel(g, r.reduced(2).toFloat(), accent);

    const bool rumble = shownEngine == EngineType::Rumble;
    const juce::String name = juce::String(engineName(shownEngine)).toUpperCase();
    const MuLookAndFeel::Section engineSection { engineBoxR, name + " ENGINE" };
    const MuLookAndFeel::Section grooveSection { grooveBoxR, rumble ? juce::String("ENVELOPE") : juce::String("GROOVE") };
    if (stepsBoxR.isEmpty()) MuLookAndFeel::drawSections(g, *this, { engineSection, grooveSection }, accent);
    else                     MuLookAndFeel::drawSections(g, *this, { engineSection, grooveSection, { stepsBoxR, "STEPS" } }, accent);

    // The shown track's colour on its engine box.
    g.setColour(layerColour());
    g.drawRoundedRectangle(engineBoxR.toFloat().expanded(1.0f), 4.0f, 1.5f);
}

void TrackContent::paintOverChildren(juce::Graphics& g)
{
    if (! MuLookAndFeel::hasScrews(*this)) return;
    for (auto r : { engineR, slotR })
        MuLookAndFeel::drawPanelScrews(g, r.reduced(2).toFloat());
}

} // namespace mu_on
