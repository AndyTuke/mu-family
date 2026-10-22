#include "TrackContent.h"
#include "Audio/SpinLock.h"   // mu-core: spin lock helpers

#include <limits>

namespace mu_on
{

namespace
{
    using Spec = mu_ui::ParamKnobGrid::Spec;

    // The Rumble envelope's time lines: one per loop unit (the "1/4" of a loop of four), so the beats show. A loop of
    // one unit has none.
    float loopUnitFraction(int multiplier) { return multiplier > 1 ? 1.0f / (float) multiplier : 0.0f; }

    // What each FM Perc knob does in each algorithm (one line, no name or value: the status bar carries those).
    // Rows are the ten knobs in grid order, columns the algorithms in stored order.
    constexpr int kNumFmPercAlgorithms = 8;
    constexpr const char* kFmPercTips[10][kNumFmPercAlgorithms] = {
        // Tune
        { "Pitch of the tom body", "Pitch of the drum", "Start pitch, before it falls", "Pitch of the tick", "Base pitch of the metal set",
          "Pitch of the bar", "Pitch of the ping", "Pitch of the burst" },
        // Ratio
        { "Skin tone: 1.0 plain, 1.5 round, 1.59 natural head", "Overtone: 1.6 skin, 2.1 bongo, 2.3 timbale", "Low is smooth, high is metallic",
          "Clave to block character", "Stretches the metal set: above 1 bell, below 1 hat", "About 4 gives a tuned mallet",
          "Whole numbers ring clean, others go inharmonic", "Odd values make it rougher" },
        // Index
        { "Brightness of the knock", "Brightness of the slap", "Grit of the zap", "Hardness of the tick", "Amount of metal",
          "Strength of the upper partial", "Brightness at the start of the ping", "Amount of crunch" },
        // FM Env
        { "How long the knock lasts", "How long the slap lasts", "Short is a blip, long is a screamer", "Length of the tick's edge",
          "How long the metal stays bright", "How long the upper partial lasts", "How long the ping stays bright", "Length of the burst's bite" },
        // Fdbk
        { "Roughness in the knock", "Roughness in the slap", "Fuzz on the zap", "Grit on the tick", "Dirt in the metal",
          "Rasp on the bar", "Sharper, more resonant ping", "High values turn to noise" },
        // Dive
        { "Pitch fall: subtle is a tonk, big is an electro tom", "Small pitch fall", "Depth of the pew", "Small pitch fall on the tick",
          "Pitch fall of the hit", "Small pitch fall", "Below zero it rises, a chirp", "Depth of the swoop" },
        // Dv Time
        { "How fast the pitch falls", "How fast the pitch falls", "Short is a thud, long is a laser", "How fast the pitch falls",
          "How fast the pitch falls", "How fast the pitch falls", "How fast it rises or falls", "How fast the swoop runs" },
        // Decay
        { "Length of the tom", "Length of the drum", "Length of the zap", "Length of the tick", "Short is closed, long is open",
          "Length of the bar ring", "Length of the ping", "Length of the burst" },
        // Fold
        { "Bite on the attack", "Bite on the attack", "Edge on the zap", "Edge on the tick", "Industrial bite",
          "Edge on the bar", "Edge on the ping", "Distortion of the burst" },
        // Noise
        { "Noise tail, the 808 \"fake reverb\"", "Noise under the hit", "Noise in the zap", "Noise in the tick", "Noise in the metal",
          "Noise under the bar", "Noise under the ping", "Noise in the burst" },
    };

    // The selected engine's controls, bound to track `track` (in display order). `algorithm` picks the FM Perc
    // tooltip column.
    std::vector<Spec> specsFor(int track, EngineType engine, int algorithm)
    {
        auto id = [&](const char* p) { return engineParamId(track, engine, p); };
        switch (engine)
        {
            case EngineType::Kick:
                return { { id("tune"), "Tune" }, { id("ptch"), "Pitch" }, { id("pdec"), "P.Dec" }, { id("adec"), "Decay" }, { id("drive"), "Drive" }, { id("fine"), "Fine" } };
            case EngineType::Bass:
                return { { id("wave"), "Wave" }, { id("tune"), "Tune" }, { id("sub"), "Sub" }, { id("cut"), "Cutoff" }, { id("res"), "Reso" },
                         { id("env"), "F.Env" }, { id("edec"), "E.Dec" }, { id("atk"), "Atk" }, { id("dec"), "Dec" }, { id("sus"), "Sus" }, { id("drive"), "Drive" }, { id("fine"), "Fine" } };
            case EngineType::Hat:   // two sets: the closed hat, then the open hat (the step row picks which a step plays)
                return { { id("tune"), "Cl.Tune" }, { id("dec"), "Cl.Decay" }, { id("otune"), "Op.Tune" }, { id("odec"), "Op.Decay" } };
            case EngineType::Snare:
                return { { id("tune"), "Tune" }, { id("dec"), "Decay" } };
            // Rumble processes another track: input low-pass → input drive → 3 delay taps → reverb → filter.
            case EngineType::Rumble:
                return { { id("inlp"), "In LP" }, { id("drive"), "Drive" }, { id("d1"), "1/16" }, { id("d2"), "2/16" }, { id("d3"), "3/16" },
                         { id("size"), "Rv Size" }, { id("revmix"), "Rv Mix" }, { id("revlp"), "Rv LP" }, { id("cut"), "Cutoff" }, { id("res"), "Reso" } };
            // FM Perc: the Algorithm selector (two cells) then the ten fixed knobs; the tips follow the algorithm.
            case EngineType::FmPerc:
            {
                const int col = juce::jlimit(0, kNumFmPercAlgorithms - 1, algorithm);
                const char* ids[10]    = { "tune", "ratio", "index", "idec", "fdbk", "dive", "dtime", "decay", "fold", "noise" };
                const char* labels[10] = { "Tune", "Ratio", "Index", "FM Env", "Fdbk", "Dive", "Dv Time", "Decay", "Fold", "Noise" };
                std::vector<Spec> specs { { id("algo"), "Algorithm" } };
                for (int k = 0; k < 10; ++k)
                {
                    Spec spec { id(ids[k]), labels[k] };
                    spec.tooltip = kFmPercTips[k][col];
                    specs.push_back(std::move(spec));
                }
                return specs;
            }
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
    : host(trackHost), grid(trackHost.getTrackParameters()), rows(trackHost), soloRow(trackHost), insertSub(base, "t")
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
    soloRow.onStatusUpdate = [this](const juce::String& name, const juce::String& value) { if (onStatusUpdate) onStatusUpdate(name, value); };
    addChildComponent(soloRow);

    // The track's own Swing / Accent, shown in the Groove box (a Rumble track shows its envelope there instead);
    // setLayer attaches them to the shown track's rows.
    addAndMakeVisible(swingKnob);
    addAndMakeVisible(accentKnob);
    for (auto* k : { &sendEffKnob, &sendDlyKnob, &sendRevKnob }) addAndMakeVisible(*k);
    soloBtn.setClickingTogglesState(true);
    addAndMakeVisible(soloBtn);
    insertSub.onStatusUpdate = [this](const juce::String& name, const juce::String& value) { if (onStatusUpdate) onStatusUpdate(name, value); };
    addAndMakeVisible(insertSub);

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
        rumbleEnvEditor.setStepFraction(loopUnitFraction(mult));
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

int TrackContent::shownAlgorithm() const
{
    const auto* algo = host.getTrackParameters().getRawParameterValue(engineParamId(currentLayer, EngineType::FmPerc, "algo"));
    return algo != nullptr ? juce::roundToInt(algo->load()) : 0;
}

// The knob grid for the shown engine; for FM Perc the tooltips describe the knobs in the current algorithm.
void TrackContent::rebuildGrid()
{
    gridAlgorithm = shownEngine == EngineType::FmPerc ? shownAlgorithm() : -1;
    auto specs = specsFor(currentLayer, shownEngine, juce::jmax(0, gridAlgorithm));
    bindModulationArcs(specs);
    grid.setSpecs(specs);
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
    rebuildGrid();

    // The groove knobs follow the shown track's own rows (the old attachments go first).
    swingAtt.reset();
    accentAtt.reset();
    swingAtt  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(host.getTrackParameters(), layerParamId(currentLayer, "swing"),  swingKnob.getSlider());
    accentAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(host.getTrackParameters(), layerParamId(currentLayer, "accent"), accentKnob.getSlider());

    // The strip's sends and solo (ch{N}_), and the track's insert.
    for (auto* a : { &sendEffAtt, &sendDlyAtt, &sendRevAtt }) a->reset();
    soloAtt.reset();
    const juce::String strip = "ch" + juce::String(currentLayer) + "_";
    auto& apvts = host.getTrackParameters();
    sendEffAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, strip + "sendEff", sendEffKnob.getSlider());
    sendDlyAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, strip + "sendDly", sendDlyKnob.getSlider());
    sendRevAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, strip + "sendRev", sendRevKnob.getSlider());
    soloAtt    = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, strip + "solo", soloBtn);
    insertSub.setChannel(currentLayer);

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
    for (auto* k : { &sendEffKnob, &sendDlyKnob, &sendRevKnob }) k->setVisible(! rumble);   // a Rumble's box holds its envelope
    soloBtn.setVisible(! rumble);
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
        rumbleEnvEditor.setStepFraction(loopUnitFraction(mult));
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
    // A new FM Perc algorithm (picked here, by automation or a preset): the tooltips describe the new voice. Done from
    // the timer, never from the selector's own callback, which must not destroy the selector it is running in.
    if (shownEngine == EngineType::FmPerc && shownAlgorithm() != gridAlgorithm) rebuildGrid();
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

    // Panel 1: ENGINE | voice | INSERT, left to right. The engine box holds the dropdown(s) (Engine, and a
    // Rumble's Source) captioned on the knobs' label band; the voice box is the engine's knob grid and takes the
    // width the other two leave; the insert box is the shared insert panel. All three are two knob rows tall.
    const bool rumble  = shownEngine == EngineType::Rumble;
    const int dropW    = s(MuLookAndFeel::kDropdownW);
    const int divW     = s(LF::kVoiceDivW);
    const int labelH   = s(mu_ui::ParamKnobGrid::kLabelH);
    const int knobH    = s(LF::kKnobSize2H);
    const int insertW  = 4 * s(LF::kKnobSize2W);
    const int insertH  = 2 * knobH + s(LF::kVoiceGap);
    const int boxH     = insertH + 2 * s(LF::kSpaceS);
    const int numDrops = (locked ? 0 : 1) + (rumble ? 1 : 0);
    const int engineW  = numDrops > 0 ? 2 * clear + dropW : 0;
    engineR    = { 0, 0, w, padY + plateH + boxH + padY };
    const int boxY = padY + plateH;
    int x = padX;
    engineBoxR = engineW > 0 ? juce::Rectangle<int>(x, boxY, engineW, boxH) : juce::Rectangle<int>();
    if (engineW > 0) x += engineW + divW;
    const int insertBoxW = 2 * clear + insertW;
    const int voiceW   = juce::jmax(0, w - padX - x - divW - insertBoxW);
    voiceBoxR  = { x, boxY, voiceW, boxH };
    insertBoxR = { voiceBoxR.getRight() + divW, boxY, insertBoxW, boxH };

    const juce::Font captionFont(juce::FontOptions{}.withHeight(mu_ui::sf(LF::kKnobLabelFont)));   // as the knob labels
    const int bandH = s(mu_ui::ParamKnobGrid::kCellH) - labelH;
    auto place = [&](juce::Label& caption, DropdownSelect& dropdown, int y)
    {
        caption.setFont(captionFont);
        caption.setBounds(engineBoxR.getX() + clear, y, dropW, labelH);
        dropdown.setBounds(engineBoxR.getX() + clear, y + labelH + (bandH - s(MuLookAndFeel::kDropdownH)) / 2, dropW, s(MuLookAndFeel::kDropdownH));
    };
    int dropY = engineBoxR.getY() + s(LF::kSpaceS);
    if (! locked) { place(engineCaption, engineSelect, dropY); dropY += s(mu_ui::ParamKnobGrid::kCellH) + gap; }
    if (rumble)   place(sourceCaption, sourceSelect, dropY);
    grid.setBounds(voiceBoxR.reduced(clear, s(LF::kSpaceS)));
    insertSub.setBounds(insertBoxR.getX() + clear, insertBoxR.getY() + s(LF::kSpaceS), insertW, insertH);

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
        // The LAYER box: three columns on the standard knob cell, Swing / Accent / Solo over the three sends.
        const int knobW = s(LF::kKnobSize2W);
        const int rowGap = s(LF::kVoiceGap);
        const int blockW = 3 * knobW + 2 * gap;
        const int blockX = g.getX() + (g.getWidth() - blockW) / 2;
        swingKnob.setBounds(blockX, g.getY(), knobW, knobH);
        accentKnob.setBounds(blockX + knobW + gap, g.getY(), knobW, knobH);
        soloBtn.setBounds(blockX + 2 * (knobW + gap), g.getY() + (knobH - s(LF::kClientButtonH)) / 2, knobW, s(LF::kClientButtonH));
        const int row2Y = g.getY() + knobH + rowGap;
        sendEffKnob.setBounds(blockX, row2Y, knobW, knobH);
        sendDlyKnob.setBounds(blockX + knobW + gap, row2Y, knobW, knobH);
        sendRevKnob.setBounds(blockX + 2 * (knobW + gap), row2Y, knobW, knobH);
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
    // Sections left to right, then the Layer box and the Steps box: each a raised box named on a plate above it.
    std::vector<MuLookAndFeel::Section> sections;
    if (! engineBoxR.isEmpty()) sections.push_back({ engineBoxR, locked ? juce::String("SOURCE") : juce::String("ENGINE") });
    sections.push_back({ voiceBoxR, name + " VOICE" });
    sections.push_back({ insertBoxR, "INSERT" });
    sections.push_back({ grooveBoxR, rumble ? juce::String("ENVELOPE") : juce::String("LAYER") });
    if (! stepsBoxR.isEmpty()) sections.push_back({ stepsBoxR, "STEPS" });
    MuLookAndFeel::drawSections(g, *this, sections, accent);

    // The shown track's colour on its voice box.
    g.setColour(layerColour());
    g.drawRoundedRectangle(voiceBoxR.toFloat().expanded(1.0f), 4.0f, 1.5f);
}

void TrackContent::paintOverChildren(juce::Graphics& g)
{
    if (! MuLookAndFeel::hasScrews(*this)) return;
    for (auto r : { engineR, slotR })
        MuLookAndFeel::drawPanelScrews(g, r.reduced(2).toFloat());
}

} // namespace mu_on
