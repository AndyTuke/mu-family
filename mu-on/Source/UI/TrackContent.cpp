#include "TrackContent.h"
#include "Audio/SpinLock.h"   // mu-core: spin lock helpers

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

TrackContent::TrackContent(PluginProcessor& processor)
    : proc(processor), grid(processor.apvts), rows(processor)
{
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

    // Global Swing / Accent, shown in the Groove box (a Rumble track shows its envelope there instead).
    addAndMakeVisible(swingKnob);
    addAndMakeVisible(accentKnob);
    swingAtt  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, "seq_swing",  swingKnob.getSlider());
    accentAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, "seq_accent", accentKnob.getSlider());

    // The Rumble track's drawable bar-volume envelope, edited under the track's own lock.
    rumbleEnvEditor.setUnipolar(true);
    rumbleEnvEditor.onChange = [this](const std::vector<ControlSequence::CurvePoint>& pts)
    {
        auto& t = proc.getTrack(currentLayer);
        mu_core::spinLock(t.rumbleEnvLock);
        t.rumbleEnv.curvePoints = pts;
        mu_core::spinUnlock(t.rumbleEnvLock);
    };
    addChildComponent(rumbleEnvEditor);

    // Its length: the same control as a modulator's Loop.
    rumbleLength.onChange = [this](NoteValue nv, NoteMod mod, int mult)
    {
        auto& t = proc.getTrack(currentLayer);
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
    return MuLookAndFeel::channelPalette[
        (size_t) (proc.getLayerColourIndex(currentLayer) % MuLookAndFeel::kChannelPaletteSize)];
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
    shownEngine = proc.engineOf(currentLayer);
    engineSelect.setSelectedId(toIndex(shownEngine) + 1, false);
    grid.setSpecs(specsFor(currentLayer, shownEngine));

    const bool rumble = shownEngine == EngineType::Rumble;
    sourceCaption.setVisible(rumble);
    sourceSelect.setVisible(rumble);
    swingKnob.setVisible(! rumble);
    accentKnob.setVisible(! rumble);
    rumbleLength.setVisible(rumble);
    rumbleEnvEditor.setVisible(rumble);

    if (rumble)
    {
        refreshSourceList();

        auto& t = proc.getTrack(currentLayer);
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
    juce::uint64 sig = (juce::uint64) proc.getNumLayers();
    for (int i = 0; i < proc.getNumLayers(); ++i)
    {
        sig = sig * 131u + (juce::uint64) toIndex(proc.engineOf(i));
        if (i != currentLayer)
            sourceSelect.addItem(juce::String(i + 1) + "  " + proc.getLayerName(i), i + 1);
    }
    shownLayerSignature = sig;
    refreshSourceSelection();
}

void TrackContent::refreshSourceSelection()
{
    const int src = (int) std::lround(proc.apvts.getRawParameterValue(layerParamId(currentLayer, "src"))->load());
    const bool usable = src >= 0 && src < proc.getNumLayers() && src != currentLayer;
    sourceSelect.setSelectedId(usable ? src + 1 : 0, false);
}

void TrackContent::engineChosen(int id)
{
    proc.selectEngine(currentLayer, engineFromIndex(id - 1));
    rebuild();
    if (onEngineChanged) onEngineChanged();
}

void TrackContent::sourceChosen(int id)
{
    if (id > 0) setParamPlain(proc.apvts, layerParamId(currentLayer, "src"), (float) (id - 1));
}

void TrackContent::timerCallback()
{
    if (currentLayer >= proc.getNumLayers()) return;   // between a delete and the editor reselecting

    // The engine changed underneath us (host automation, a preset): show the new one.
    if (proc.engineOf(currentLayer) != shownEngine)
    {
        rebuild();
        if (onEngineChanged) onEngineChanged();
        return;
    }
    if (shownEngine != EngineType::Rumble) return;

    // Keep the Source list current (layers added, deleted, re-engined) and the envelope's playhead moving.
    juce::uint64 sig = (juce::uint64) proc.getNumLayers();
    for (int i = 0; i < proc.getNumLayers(); ++i) sig = sig * 131u + (juce::uint64) toIndex(proc.engineOf(i));
    if (sig != shownLayerSignature) refreshSourceList();
    else                            refreshSourceSelection();

    const double beat = proc.getInternalBeatPos();
    const double len = juce::jmax(1.0e-6, proc.getTrack(currentLayer).rumbleEnv.getLoopLengthBeats());
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
    const int leftW    = dropW + (rumble ? gap + dropW : 0);
    engineR    = { 0, 0, w, padY + plateH + boxH + padY };
    const int boxW = juce::jmin(w - 2 * padX, 2 * clear + leftW + gap + grid.getPreferredWidth());
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
    place(engineCaption, engineSelect, inner.getX());
    if (rumble) place(sourceCaption, sourceSelect, inner.getX() + dropW + gap);
    grid.setBounds(inner.withTrimmedLeft(leftW + gap));

    // Panel 2 (flex): Groove | Steps, both the full height left under their plates.
    slotR = { 0, engineR.getBottom(), w, juce::jmax(0, h - engineR.getBottom()) };
    const int boxesY = slotR.getY() + padY + plateH;
    const int boxesH = juce::jmax(0, slotR.getHeight() - 2 * padY - plateH);
    // The left box of the second panel is as wide for Swing / Accent as for the Rumble envelope.
    grooveBoxR = { padX, boxesY, s(MuLookAndFeel::kGrooveBoxW), boxesH };
    const int stepsX = grooveBoxR.getRight() + s(LF::kVoiceDivW);
    stepsBoxR  = { stepsX, boxesY, juce::jmax(0, w - padX - stepsX), boxesH };
    rows.setBounds(stepsBoxR.reduced(clear, s(LF::kSpaceS)));

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
    MuLookAndFeel::drawSections(g, *this, { { engineBoxR, name + " ENGINE" },
                                            { grooveBoxR, rumble ? juce::String("ENVELOPE") : juce::String("GROOVE") },
                                            { stepsBoxR, "STEPS" } }, accent);

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
