#include "Audio/ChordVoicing.h"   // the chord list and note names
#include "TrackContent.h"
#include "Audio/SpinLock.h"   // mu-core: spin lock helpers

#include <limits>
#include "Audio/MusicalScales.h"   // mu-core: the scale names
#include "UI/ParamChoices.h"          // mu-core: selector items from choice parameters

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
    std::vector<Spec> voiceSpecsFor(int track, EngineType engine, int algorithm)
    {
        auto id = [&](const char* p) { return engineParamId(track, engine, p); };
        switch (engine)
        {
            case EngineType::Kick:
            {
                std::vector<Spec> specs { { id("tune"), "Tune" }, { id("ptch"), "Pitch" }, { id("pdec"), "P.Dec" }, { id("adec"), "Decay" }, { id("drive"), "Drive" }, { id("fine"), "Fine" } };
                specs[0].statusName = "Kick Tune";  specs[1].statusName = "Kick Pitch Drop";  specs[2].statusName = "Kick Pitch Decay";
                specs[3].statusName = "Kick Decay"; specs[4].statusName = "Kick Drive";       specs[5].statusName = "Kick Fine Tune";
                return specs;
            }
            case EngineType::Bass:
            {
                // Row 1: the oscillator (with its Glide) and the filter (with the Drive); row 2: the amp envelope. Each group's
                // space is shared out across the panel.
                std::vector<Spec> specs { { id("wave"), "Wave" }, { id("tune"), "Tune" }, { id("fine"), "Fine" }, { id("sub"), "Sub" }, { id("glide"), "Glide" },
                                          { id("cut"), "Cutoff" }, { id("res"), "Reso" }, { id("env"), "F.Env" }, { id("edec"), "E.Dec" }, { id("drive"), "Drive" },
                                          { id("atk"), "Atk" }, { id("dec"), "Dec" }, { id("sus"), "Sus" }, { id("rel"), "Rel" } };
                specs[0].groupStart = specs[5].groupStart = specs[10].groupStart = true;
                specs[10].newRow = true;
                specs[4].tooltip = "Slide time into a tied step (drag across two steps to tie them)";
                const char* const names[] = { "Bass Wave", "Bass Tune", "Bass Fine Tune", "Bass Sub", "Bass Glide", "Bass Filter Cutoff", "Bass Filter Resonance",
                                              "Bass Filter Envelope", "Bass Filter Env Decay", "Bass Drive", "Bass Amp Attack", "Bass Amp Decay", "Bass Amp Sustain", "Bass Amp Release" };
                for (size_t i = 0; i < specs.size(); ++i) specs[i].statusName = names[i];
                return specs;
            }
            case EngineType::Chord:
            {
                // The wave on the first row; the amp's attack, decay, sustain and release as smaller knobs on a second row.
                std::vector<Spec> specs { { id("wave"), "Wave" }, { id("atk"), "Atk" }, { id("dec"), "Dec" }, { id("sus"), "Sus" }, { id("rel"), "Rel" } };
                specs[0].statusName = "Chord Wave";
                specs[1].statusName = "Chord Amp Attack";  specs[2].statusName = "Chord Amp Decay";
                specs[3].statusName = "Chord Amp Sustain"; specs[4].statusName = "Chord Amp Release";
                specs[1].newRow = true;
                for (size_t k = 1; k < specs.size(); ++k) specs[k].small = true;
                return specs;
            }
            case EngineType::Hat:   // two sets: the closed hat, then the open hat (the step row picks which a step plays)
                return { { id("tune"), "Cl.Tune", {}, {}, {}, {}, false, false, "Hat Closed Tune" }, { id("dec"), "Cl.Decay", {}, {}, {}, {}, false, false, "Hat Closed Decay" },
                         { id("otune"), "Op.Tune", {}, {}, {}, {}, false, false, "Hat Open Tune" }, { id("odec"), "Op.Decay", {}, {}, {}, {}, false, false, "Hat Open Decay" } };
            case EngineType::Snare:
                return { { id("tune"), "Tune", {}, {}, {}, {}, false, false, "Snare Tune" }, { id("dec"), "Decay", {}, {}, {}, {}, false, false, "Snare Decay" },
                         { id("snap"), "Snap", {}, {}, {}, {}, false, false, "Snare Snap" } };
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

    // The selected engine's voice knobs, left-justified in their own section. (A drum or percussion engine's filter has a section of
    // its own, filterSpecsFor.)
    std::vector<Spec> specsFor(int track, EngineType engine, int algorithm)
    {
        return voiceSpecsFor(track, engine, algorithm);
    }

    // A Kick, Hat, Snare or FM Perc filter section, the same on each: row one the type, cutoff and resonance, row two the
    // envelope's attack, decay and depth as smaller knobs, so the controls sit in the same places whichever engine is shown.
    std::vector<Spec> filterSpecsFor(int track, EngineType engine)
    {
        if (engine == EngineType::Chord)
        {
            // The same section, with an A/D/S/R envelope: Cutoff, Reso and Drive, then Atk, Dec, Sus, Rel and Depth as smaller knobs.
            auto cid = [&](const char* p) { return engineParamId(track, engine, p); };
            Spec cut { cid("cut"), "Cutoff" };  cut.statusName = "Chord Filter Cutoff";
            Spec res { cid("res"), "Reso" };    res.statusName = "Chord Filter Resonance";
            Spec drv { cid("drive"), "Drive" }; drv.statusName = "Chord Filter Drive";
            Spec atk { cid("fatk"), "Atk" };    atk.statusName = "Chord Filter Env Attack";   atk.newRow = true;  atk.small = true;
            Spec dec { cid("edec"), "Dec" };    dec.statusName = "Chord Filter Env Decay";    dec.small = true;
            Spec sus { cid("fsus"), "Sus" };    sus.statusName = "Chord Filter Env Sustain";  sus.small = true;
            Spec rel { cid("frel"), "Rel" };    rel.statusName = "Chord Filter Env Release";  rel.small = true;
            Spec dep { cid("env"), "Depth" };   dep.statusName = "Chord Filter Env Depth";    dep.small = true;
            dep.tooltip = "How far the filter envelope opens the cutoff on each chord";
            return { std::move(cut), std::move(res), std::move(drv), std::move(atk), std::move(dec), std::move(sus), std::move(rel), std::move(dep) };
        }
        const char* name = engine == EngineType::Kick ? "Kick" : engine == EngineType::Hat ? "Hat" : engine == EngineType::Snare ? "Snare"
                         : engine == EngineType::FmPerc ? "FM Perc" : nullptr;
        if (name == nullptr) return {};
        auto id = [&](const char* p) { return engineParamId(track, engine, p); };
        const juce::String nm(name);
        Spec type { id("ftype"), "Type" };
        type.statusName = nm + " Filter Type";
        type.tooltip = "LP12, HP12, BP12 or Notch12. LP12 at full cutoff is off.";
        Spec cut { id("fcut"), "Cutoff" };  cut.statusName = nm + " Filter Cutoff";
        Spec res { id("fres"), "Reso" };    res.statusName = nm + " Filter Resonance";
        Spec atk { id("fatk"), "Atk" };     atk.statusName = nm + " Filter Env Attack";   atk.newRow = true;  atk.small = true;
        Spec dec { id("fdec"), "Dec" };     dec.statusName = nm + " Filter Env Decay";    dec.small = true;
        Spec dep { id("fdep"), "Depth" };   dep.statusName = nm + " Filter Env Depth";    dep.small = true;
        atk.tooltip = "The filter envelope: how fast the cutoff moves out on each step";
        dec.tooltip = "The filter envelope: how long it takes to fall back";
        dep.tooltip = "How far the envelope moves the cutoff: up for positive, down for negative";
        return { std::move(type), std::move(cut), std::move(res), std::move(atk), std::move(dec), std::move(dep) };
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
    : host(trackHost), grid(trackHost.getTrackParameters()), filterGrid(trackHost.getTrackParameters()), rows(trackHost), soloRow(trackHost), insertSub(base, "t")
{
    for (int e = 0; e < kNumEngineTypes; ++e) engineSelect.addItem(engineName(engineFromIndex(e)), e + 1);
    engineSelect.onChange = [this](int id) { engineChosen(id); };
    sourceSelect.onChange = [this](int id) { sourceChosen(id); };

    for (auto* cap : { &engineCaption, &sourceCaption, &rootCaption, &scaleCaption })
    {
        cap->setJustificationType(juce::Justification::centred);
        cap->setInterceptsMouseClicks(false, false);
        addAndMakeVisible(*cap);
    }
    addAndMakeVisible(engineSelect);
    // The master key and scale: global rows bound once (a host without them, none).
    if (host.getTrackParameters().getParameter(kRootParamId) != nullptr)
    {
        for (int i = 0; i < mu_audio::kNumScales; ++i) scaleSelect.addItem(mu_audio::kScales[(size_t) i].name, i + 1);
        mu_ui::addChoiceItems(rootSelect, host.getTrackParameters(), kRootParamId);   // items from the parameter's choices
        rootAtt  = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(host.getTrackParameters(), kRootParamId,  rootSelect.getComboBox());
        scaleAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(host.getTrackParameters(), kScaleParamId, scaleSelect.getComboBox());
    }
    addChildComponent(rootSelect);
    addChildComponent(scaleSelect);
    addChildComponent(sourceSelect);
    sourceCaption.setVisible(false);
    addAndMakeVisible(grid);
    grid.onStatusUpdate = [this](const juce::String& name, const juce::String& value) { if (onStatusUpdate) onStatusUpdate(name, value); };
    addAndMakeVisible(filterGrid);
    filterGrid.onStatusUpdate = grid.onStatusUpdate;
    // The Layer box's knobs and the selectors report to the status bar under their full names.
    auto report = [this](KnobWithLabel& k, const juce::String& name)
    {
        k.onStatusUpdate = [this, name](const juce::String&, const juce::String& value) { if (onStatusUpdate) onStatusUpdate(name, value); };
    };
    report(swingKnob, "Layer Swing");  report(accentKnob, "Layer Accent");  report(ghostKnob, "Layer Ghost");  report(probKnob, "Layer Probability");  report(levelKnob, "Track Level");
    report(sendEffKnob, "FX Send");   report(sendDlyKnob, "Delay Send");   report(sendRevKnob, "Reverb Send");
    auto say = [this](const juce::String& name, DropdownSelect& d) { if (onStatusUpdate) onStatusUpdate(name, d.getText()); };
    rootSelect.getComboBox().onChange  = [this, say] { say("Key Root", rootSelect); };
    scaleSelect.getComboBox().onChange = [this, say] { say("Key Scale", scaleSelect); };

    rows.onRowClicked = [this](int row) { if (onRowClicked) onRowClicked(row); };
    addAndMakeVisible(rows);
    soloRow.onStatusUpdate = [this](const juce::String& name, const juce::String& value) { if (onStatusUpdate) onStatusUpdate(name, value); };
    addChildComponent(soloRow);
    soloPager.pageCount = [this] { return host.getTrack(currentLayer).row.numPages(); };
    soloPager.lengthRow = [this]() -> StepRow* { return &host.getTrack(currentLayer).row; };
    soloPager.onChanged = [this] { soloRow.setFirstStep(soloPager.page() * StepRow::kNumSteps); };
    addChildComponent(soloPager);

    // The track's own Swing / Accent, shown in the Groove box (a Rumble track shows its envelope there instead);
    // setLayer attaches them to the shown track's rows.
    addAndMakeVisible(swingKnob);
    addAndMakeVisible(accentKnob);
    addAndMakeVisible(ghostKnob);
    for (auto* k : { &sendEffKnob, &sendDlyKnob, &sendRevKnob, &probKnob, &levelKnob }) addAndMakeVisible(*k);
    // The STEP panel's controls edit the selected step of the shown track's row.
    for (auto* cap : { &loopCaption, &voiceCaption, &octaveCaption, &noteCaption, &chordCaption })
    {
        cap->setJustificationType(juce::Justification::centred);
        cap->setInterceptsMouseClicks(false, false);
        addAndMakeVisible(*cap);
    }
    stepProbKnob.setRange(0.0, 100.0, 1.0);
    stepProbKnob.getSlider().setTextValueSuffix(" %");
    stepProbKnob.onValueChanged = [this](double v) { host.getTrack(currentLayer).row.setStepProb(selectedStepOf[(size_t) currentLayer], juce::roundToInt(v)); };
    stepProbKnob.onStatusUpdate = [this](const juce::String&, const juce::String& value) { if (onStatusUpdate) onStatusUpdate("Step Probability", value); };
    addAndMakeVisible(stepProbKnob);
    for (int i = 0; i < (int) std::size(kLoopRatios); ++i) loopSelect.addItem(juce::String(kLoopRatios[i].play) + ":" + juce::String(kLoopRatios[i].of), i + 1);
    loopSelect.onChange = [this](int id) { host.getTrack(currentLayer).row.setStepLoop(selectedStepOf[(size_t) currentLayer], id - 1); if (onStatusUpdate) onStatusUpdate("Step Play", loopSelect.getText()); };
    voiceSelect.addItem("Closed", 1);
    voiceSelect.addItem("Open", 2);
    voiceSelect.onChange = [this](int id) { host.getTrack(currentLayer).row.setOpen(selectedStepOf[(size_t) currentLayer], id == 2); if (onStatusUpdate) onStatusUpdate("Step Sound", voiceSelect.getText()); };
    for (int o = -2; o <= 2; ++o) octaveSelect.addItem((o > 0 ? "+" : "") + juce::String(o), o + 3);
    octaveSelect.onChange = [this](int id) { host.getTrack(currentLayer).row.setStepOctave(selectedStepOf[(size_t) currentLayer], id - 3); refreshNoteItems(); if (onStatusUpdate) onStatusUpdate("Step Octave", octaveSelect.getText()); };
    noteSelect.onChange   = [this](int id) { host.getTrack(currentLayer).row.setStepNote(selectedStepOf[(size_t) currentLayer], id - 13); if (onStatusUpdate) onStatusUpdate("Step Note", noteSelect.getText()); };
    addAndMakeVisible(loopSelect);
    addChildComponent(voiceSelect);
    addChildComponent(octaveSelect);
    addChildComponent(noteSelect);
    for (int i = 0; i < kNumChords; ++i) chordSelect.addItem(kChords[(size_t) i].name, i + 1);
    chordSelect.onChange = [this](int id) { host.getTrack(currentLayer).row.setStepChord(selectedStepOf[(size_t) currentLayer], id - 1); if (onStatusUpdate) onStatusUpdate("Step Chord", chordSelect.getText()); };
    for (auto* k : { &invKnob, &spreadKnob }) addChildComponent(*k);
    invKnob.setRange(0.0, (double) kMaxChordInversion, 1.0);
    spreadKnob.setRange(0.0, (double) kMaxChordSpread, 1.0);
    invKnob.onValueChanged = [this](double v) { host.getTrack(currentLayer).row.setStepInversion(selectedStepOf[(size_t) currentLayer], juce::roundToInt(v)); };
    spreadKnob.onValueChanged = [this](double v) { host.getTrack(currentLayer).row.setStepSpread(selectedStepOf[(size_t) currentLayer], juce::roundToInt(v)); };
    invKnob.onStatusUpdate = [this](const juce::String&, const juce::String& v) { if (onStatusUpdate) onStatusUpdate("Step Inversion", v); };
    spreadKnob.onStatusUpdate = [this](const juce::String&, const juce::String& v) { if (onStatusUpdate) onStatusUpdate("Step Spread", v); };
    addChildComponent(chordCaption);
    addChildComponent(chordSelect);
    rows.onStepSelected = [this](int, int step) { selectStep(step); };
    soloRow.onStepSelected = [this](int step) { selectStep(step); };
    soloBtn.setClickingTogglesState(true);
    addAndMakeVisible(soloBtn);
    insertSub.onStatusUpdate = [this](const juce::String& name, const juce::String& value) { if (onStatusUpdate) onStatusUpdate(name, value); };
    insertSub.onInsertAlgorithmChanged = [this](int algo) { if (onInsertAlgorithmChanged) onInsertAlgorithmChanged(algo); };
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

    // The filter section (a drum or percussion engine) and the Ghost knob's live arc.
    auto filterSpecs = filterSpecsFor(currentLayer, shownEngine);
    bindModulationArcs(filterSpecs);
    filterGrid.setSpecs(filterSpecs);
    filterGrid.setVisible(! filterSpecs.empty());
    ghostKnob.clearModBinding();
    if (! filterSpecs.empty())   // a drum, percussion or Chord engine: all have ghost steps
    {
        static const float kNaN = std::numeric_limits<float>::quiet_NaN();
        int count = 0;
        const ModDestEntry* table = destsForEngine(shownEngine, count);
        const int dest = mu_mod::indexOfParam(table, (std::size_t) count, "ghost");
        if (dest >= 0)
        {
            const int layer = currentLayer;
            const EngineType engine = shownEngine;
            ghostKnob.bindModulation(table[dest].id, &host.getTrack(currentLayer).modulationMatrix,
                                     [this, layer, engine, dest]() -> float { return host.isTrackTransportPlaying() ? host.getModLive(layer, engine, dest) : kNaN; });
        }
    }
    resized();   // the filter section comes and goes with the engine
}

void TrackContent::setLayer(int idx)
{
    currentLayer = juce::jlimit(0, mu_limits::kMaxLayers - 1, idx);
    rebuild();
}

// The STEP panel follows the shown layer's selected step: its probability, loop ratio and (a Hat) sound, read from the row.
// The Note list shows the note each offset gives (the tune after the master key and scale, plus the step's octave and the
// offset); a note outside the scale is red.
void TrackContent::refreshNoteItems()
{
    if (shownEngine != EngineType::Kick && shownEngine != EngineType::Bass && shownEngine != EngineType::Chord) return;
    const auto key = TrackStepRow::tuneKey(host, currentLayer, shownEngine);
    const auto& row = host.getTrack(currentLayer).row;
    const int step = selectedStepOf[(size_t) currentLayer];
    if (shownEngine == EngineType::Chord)
    {
        // A Chord step's root is a degree of the master scale: the list is the scale's notes, named in the key.
        noteSignature = key.root * 16 + key.scale + 100000 * (row.stepOctave(step) + 2);
        noteSelect.clear();
        const int count = mu_audio::kScales[(size_t) mu_audio::clampScaleIndex(key.scale)].count;
        for (int d = 0; d < count; ++d) noteSelect.addItem(juce::String(d + 1) + "  " + chordRootName(key.root, key.scale, d), d + 13);
        noteSelect.setSelectedId(juce::jlimit(0, count - 1, row.stepNote(step)) + 13, false);
        return;
    }
    const int octaveShift = row.stepOctave(step) * 12;
    noteSignature = (key.baseMidi * 16 + key.root) * 16 + key.scale + 100000 * (row.stepOctave(step) + 2);
    noteSelect.clear();
    for (int n = -12; n <= 12; ++n)
    {
        const int midi = key.baseMidi + octaveShift + n;
        const auto text = mu_audio::midiNoteName(midi) + (n == 0 ? juce::String() : " (" + juce::String(n > 0 ? "+" : "") + juce::String(n) + ")");
        if (mu_audio::midiInScale(midi, key.root, key.scale)) noteSelect.addItem(text, n + 13);
        else                                                  noteSelect.addItem(text, n + 13, juce::Colour(0xffff6060));   // out of the scale: red
    }
    noteSelect.setSelectedId(row.stepNote(step) + 13, false);
}

void TrackContent::refreshStepPanel()
{
    const auto& row = host.getTrack(currentLayer).row;
    const int step = selectedStepOf[(size_t) currentLayer];
    stepProbKnob.setValue(row.stepProb(step), juce::dontSendNotification);
    loopSelect.setSelectedId(row.stepLoop(step) + 1, false);
    const bool hat = shownEngine == EngineType::Hat;
    voiceCaption.setVisible(hat);
    voiceSelect.setVisible(hat);
    voiceSelect.setSelectedId(row.isOpen(step) ? 2 : 1, false);
    const bool chordEng = shownEngine == EngineType::Chord;
    const bool pitched = shownEngine == EngineType::Kick || shownEngine == EngineType::Bass || chordEng;
    for (auto* c : { (juce::Component*) &octaveCaption, (juce::Component*) &octaveSelect, (juce::Component*) &noteCaption, (juce::Component*) &noteSelect }) c->setVisible(pitched);
    for (auto* c : { (juce::Component*) &chordCaption, (juce::Component*) &chordSelect, (juce::Component*) &invKnob, (juce::Component*) &spreadKnob }) c->setVisible(chordEng);
    noteCaption.setText(chordEng ? "Root" : "Note", juce::dontSendNotification);
    chordSelect.setSelectedId(row.stepChord(step) + 1, false);
    invKnob.setValue(row.stepInversion(step), juce::dontSendNotification);
    spreadKnob.setValue(row.stepSpread(step), juce::dontSendNotification);
    octaveSelect.setSelectedId(row.stepOctave(step) + 3, false);
    refreshNoteItems();
    rows.setSelectedStep(step);
    soloRow.setSelectedStep(step);
    repaint();
}

// A right-click selected `step` in the shown layer's row (the row clicked has become the shown layer).
void TrackContent::selectStep(int step)
{
    selectedStepOf[(size_t) currentLayer] = juce::jlimit(0, StepRow::kMaxSteps - 1, step);
    refreshStepPanel();
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
    ghostAtt.reset();
    swingAtt  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(host.getTrackParameters(), layerParamId(currentLayer, "swing"),  swingKnob.getSlider());
    accentAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(host.getTrackParameters(), layerParamId(currentLayer, "accent"), accentKnob.getSlider());
    ghostAtt  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(host.getTrackParameters(), layerParamId(currentLayer, "ghost"),  ghostKnob.getSlider());

    // The strip's sends and solo (ch{N}_), and the track's insert.
    for (auto* a : { &sendEffAtt, &sendDlyAtt, &sendRevAtt, &probAtt, &levelAtt }) a->reset();
    soloAtt.reset();
    const juce::String strip = "ch" + juce::String(currentLayer) + "_";
    auto& apvts = host.getTrackParameters();
    sendEffAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, strip + "sendEff", sendEffKnob.getSlider());
    sendDlyAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, strip + "sendDly", sendDlyKnob.getSlider());
    sendRevAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, strip + "sendRev", sendRevKnob.getSlider());
    soloAtt    = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, strip + "solo", soloBtn);
    probAtt    = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, layerParamId(currentLayer, "prob"), probKnob.getSlider());
    levelAtt   = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, layerParamId(currentLayer, "level"), levelKnob.getSlider());
    insertSub.setChannel(currentLayer);

    // A host that fixes the track's engine (engineLocked) hides the Engine selector and shows the track's one
    // row of steps; mu-On never locks, so its own look is unchanged.
    locked = host.getTrack(currentLayer).engineLocked;
    engineCaption.setVisible(! locked);
    engineSelect.setVisible(! locked);
    rows.setVisible(! locked);
    const bool rumble = shownEngine == EngineType::Rumble;
    soloRow.setVisible(locked && ! rumble);   // a locked Rumble has no steps: it follows its source
    soloPager.setVisible(locked && ! rumble);
    soloPager.refresh();
    soloRow.setFirstStep(soloPager.page() * StepRow::kNumSteps);
    soloRow.setLayer(currentLayer);

    sourceCaption.setVisible(rumble);
    sourceSelect.setVisible(rumble);
    const bool showKey = ! locked && ! rumble && rootAtt != nullptr;   // the Source takes a Rumble's second cell
    for (auto* c : { (juce::Component*) &rootCaption, (juce::Component*) &rootSelect, (juce::Component*) &scaleCaption, (juce::Component*) &scaleSelect }) c->setVisible(showKey);
    swingKnob.setVisible(! rumble);
    accentKnob.setVisible(! rumble);
    // Ghost steps are for the drum, percussion and Chord engines.
    ghostKnob.setVisible(shownEngine == EngineType::Kick || shownEngine == EngineType::Hat || shownEngine == EngineType::Snare || shownEngine == EngineType::FmPerc || shownEngine == EngineType::Chord);
    for (auto* k : { &sendEffKnob, &sendDlyKnob, &sendRevKnob, &probKnob, &levelKnob }) k->setVisible(! rumble);   // a Rumble's box holds its envelope
    for (auto* c : { (juce::Component*) &stepProbKnob, (juce::Component*) &loopCaption, (juce::Component*) &loopSelect }) c->setVisible(! rumble);
    refreshStepPanel();
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
    // The Note list follows the tune, key and scale (a knob, automation or a preset moves them).
    if (shownEngine == EngineType::Kick || shownEngine == EngineType::Bass)
    {
        const auto key = TrackStepRow::tuneKey(host, currentLayer, shownEngine);
        const int octave = host.getTrack(currentLayer).row.stepOctave(selectedStepOf[(size_t) currentLayer]);
        if ((key.baseMidi * 16 + key.root) * 16 + key.scale + 100000 * (octave + 2) != noteSignature) refreshNoteItems();
    }
    else if (shownEngine == EngineType::Chord)
    {
        const auto key = TrackStepRow::tuneKey(host, currentLayer, shownEngine);
        const int octave = host.getTrack(currentLayer).row.stepOctave(selectedStepOf[(size_t) currentLayer]);
        if (key.root * 16 + key.scale + 100000 * (octave + 2) != noteSignature) refreshNoteItems();
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
    // The master key and scale (global, not per track) have a KEY section of their own, Root over Scale.
    const bool showKeyBox = ! locked && ! rumble && rootAtt != nullptr;
    const int keyW     = showKeyBox ? 2 * clear + s(LF::kTonalScaleW) : 0;
    engineR    = { 0, 0, w, padY + plateH + boxH + padY };
    const int boxY = padY + plateH;
    int x = padX;
    // The KEY section comes first: the key and scale belong to the whole instrument, not to the shown track.
    keyBoxR = keyW > 0 ? juce::Rectangle<int>(x, boxY, keyW, boxH) : juce::Rectangle<int>();
    if (keyW > 0) x += keyW + divW;
    engineBoxR = engineW > 0 ? juce::Rectangle<int>(x, boxY, engineW, boxH) : juce::Rectangle<int>();
    if (engineW > 0) x += engineW + divW;
    const int insertBoxW = 2 * clear + insertW;
    // A drum or percussion engine's FILTER section sits between the voice and the insert, four knob cells wide: the same place, the
    // same size on each engine.
    const bool hasFilter = filterGrid.isVisible();
    const int filterCells = shownEngine == EngineType::Chord ? 5 : 4;   // a Chord's envelope row is Atk, Dec, Sus, Rel and Depth
    const int filterW  = hasFilter ? 2 * clear + filterCells * (s(LF::kKnobSize2W) + s(6)) : 0;
    const int voiceW   = juce::jmax(0, w - padX - x - divW - insertBoxW - (hasFilter ? divW + filterW : 0));
    voiceBoxR  = { x, boxY, voiceW, boxH };
    filterBoxR = hasFilter ? juce::Rectangle<int>(voiceBoxR.getRight() + divW, boxY, filterW, boxH) : juce::Rectangle<int>();
    insertBoxR = { (hasFilter ? filterBoxR : voiceBoxR).getRight() + divW, boxY, insertBoxW, boxH };

    const juce::Font captionFont(juce::FontOptions{}.withHeight(mu_ui::sf(LF::kKnobLabelFont)));   // as the knob labels
    auto place = [&](juce::Label& caption, DropdownSelect& dropdown, int y)
    {
        caption.setFont(captionFont);
        caption.setBounds(engineBoxR.getX() + clear, y, dropW, labelH);
        dropdown.setBounds(engineBoxR.getX() + clear, y + labelH + s(2), dropW, s(MuLookAndFeel::kDropdownH));
    };
    int dropY = engineBoxR.getY() + s(LF::kSpaceS);
    const int rowPitch = labelH + s(2) + s(MuLookAndFeel::kDropdownH) + 2 * gap;   // a caption over its dropdown, then a gap
    if (! locked) { place(engineCaption, engineSelect, dropY); dropY += rowPitch; }
    if (rumble)   place(sourceCaption, sourceSelect, dropY);
    else if (! locked)
    {
        // Root over Scale in the KEY section, each captioned like the Engine dropdown.
        const int keyX = keyBoxR.getX() + clear, keyDdW = s(LF::kTonalScaleW);
        int keyY = keyBoxR.getY() + s(LF::kSpaceS);
        for (auto* cap : { &rootCaption, &scaleCaption }) cap->setFont(captionFont);
        rootCaption .setBounds(keyX, keyY, keyDdW, labelH);
        rootSelect  .setBounds(keyX, keyY + labelH + s(2), keyDdW, s(MuLookAndFeel::kDropdownH));
        keyY += rowPitch;
        scaleCaption.setBounds(keyX, keyY, keyDdW, labelH);
        scaleSelect .setBounds(keyX, keyY + labelH + s(2), keyDdW, s(MuLookAndFeel::kDropdownH));
    }
    grid.setMinSizeIndex(0);
    filterGrid.setMinSizeIndex(0);
    grid.setBounds(voiceBoxR.reduced(clear, s(LF::kSpaceS)));
    filterGrid.setBounds(filterBoxR.reduced(clear, s(LF::kSpaceS)));
    // The two grids draw their knobs at one size: the smaller either needs to fit.
    const int knobSize = juce::jmax(grid.fitIndex(), hasFilter ? filterGrid.fitIndex() : 0);
    grid.setMinSizeIndex(knobSize);
    filterGrid.setMinSizeIndex(knobSize);
    insertSub.setBounds(insertBoxR.getX() + clear, insertBoxR.getY() + s(LF::kSpaceS), insertW, insertH);

    // Panel 2 (flex): Groove | Steps, both the full height left under their plates.
    slotR = { 0, engineR.getBottom(), w, juce::jmax(0, h - engineR.getBottom()) };
    const int boxesY = slotR.getY() + padY + plateH;
    const int boxesH = juce::jmax(0, slotR.getHeight() - 2 * padY - plateH);
    // The left box of the second panel is as wide for Swing / Accent as for the Rumble envelope.
    // A locked Rumble has no step row, so its envelope takes the whole panel.
    const bool noSteps = locked && rumble;
    // LAYER: three knob columns wide (Rumble: the envelope editor, a fixed wider box); STEP: one dropdown wide.
    // The Steps box starts at the same x on every engine (the pane must not jump when the track changes): the left side
    // always spans the widest Groove box plus the widest Step box (a Chord's two columns), and a narrower engine fills it.
    // Each box is only as wide as the widest engine needs: the Layer box three knobs, the Step box a Chord's list column
    // beside its knob column; a Rumble's envelope box spans both (or its own minimum, if that is wider).
    const int grooveWidest = 3 * s(LF::kKnobSize2W) + 2 * gap + 2 * clear;
    const int stepWidest   = 2 * clear + s(MuLookAndFeel::kDropdownW) + gap + s(LF::kKnobSize2W);
    const int rumbleWidest = juce::jmax(grooveWidest + s(LF::kVoiceDivW) + stepWidest, 2 * clear + s(NoteLengthControl::kWidth));
    const int layerW = noSteps ? juce::jmax(0, w - 2 * padX) : (rumble ? rumbleWidest : grooveWidest);
    grooveBoxR = { padX, boxesY, layerW, boxesH };
    int rightEdge = grooveBoxR.getRight();
    stepBoxR   = (noSteps || rumble) ? juce::Rectangle<int>() : juce::Rectangle<int>(rightEdge + s(LF::kVoiceDivW), boxesY, stepWidest, boxesH);
    if (! stepBoxR.isEmpty()) rightEdge = stepBoxR.getRight();    const int stepsX = rightEdge + s(LF::kVoiceDivW);
    stepsBoxR  = noSteps ? juce::Rectangle<int>() : juce::Rectangle<int>(stepsX, boxesY, juce::jmax(0, w - padX - stepsX), boxesH);
    const auto stepsInner = stepsBoxR.reduced(clear, s(LF::kSpaceS));
    rows.setBounds(stepsInner);
    // The lone row is as tall as one of the rows above is, top-aligned (the rest of the box stays blank metal).
    soloRow.setBounds(stepsInner.withHeight(stepsInner.getHeight() / mu_limits::kLayersPerPage));
    soloPager.setBounds(stepsInner.withTop(stepsInner.getBottom() - s(StepPager::kHeight)));

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
        // Third row: the layer's Probability and Level.
        probKnob.setBounds(blockX, row2Y + knobH + rowGap, knobW, knobH);
        levelKnob.setBounds(blockX + knobW + gap, row2Y + knobH + rowGap, knobW, knobH);
        ghostKnob.setBounds(blockX + 2 * (knobW + gap), row2Y + knobH + rowGap, knobW, knobH);
    }
    layoutStepPanel();
}

void TrackContent::layoutStepPanel()
{
    using LF = MuLookAndFeel;
    using mu_ui::s;
    if (stepBoxR.isEmpty()) return;
    const auto in = stepBoxR.reduced(s(LF::kSubPanelScrewClear), s(LF::kSpaceS));
    const int labelH = s(mu_ui::ParamKnobGrid::kLabelH), knobH = s(LF::kKnobSize2H), dd = s(LF::kDropdownH), gap = s(LF::kSpaceS);
    const juce::Font captionFont(juce::FontOptions{}.withHeight(mu_ui::sf(LF::kKnobLabelFont)));
    auto placeDropdown = [&](juce::Label& caption, DropdownSelect& select, juce::Rectangle<int> column, int& y)
    {
        caption.setFont(captionFont);
        caption.setBounds(column.getX(), y, column.getWidth(), labelH);
        select.setBounds(column.getX(), y + labelH + s(2), column.getWidth(), dd);
        y += labelH + s(2) + dd + gap;
    };

    // A Chord step has two columns: the Root, Octave, Chord and Play lists, then the Probability, Inversion and Spread knobs.
    if (shownEngine == EngineType::Chord)
    {
        auto left = in.withWidth(s(LF::kDropdownW));
        const int knobX = left.getRight() + gap;
        int y = in.getY();
        placeDropdown(noteCaption, noteSelect, left, y);
        placeDropdown(octaveCaption, octaveSelect, left, y);
        placeDropdown(chordCaption, chordSelect, left, y);
        placeDropdown(loopCaption, loopSelect, left, y);
        int ky = in.getY();
        for (auto* k : { &stepProbKnob, &invKnob, &spreadKnob })
        {
            k->setBounds(knobX, ky, s(LF::kKnobSize2W), knobH);
            ky += knobH + gap;
        }
        return;
    }

    stepProbKnob.setBounds(in.getX() + (in.getWidth() - s(LF::kKnobSize2W)) / 2, in.getY(), s(LF::kKnobSize2W), knobH);
    int y = in.getY() + knobH + gap;
    placeDropdown(loopCaption, loopSelect, in, y);
    // A Hat's Sound, or a Kick or Bass step's Octave then Note, in the slots after Play.
    int yHat = y;
    placeDropdown(voiceCaption, voiceSelect, in, yHat);
    placeDropdown(octaveCaption, octaveSelect, in, y);
    placeDropdown(noteCaption, noteSelect, in, y);
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
    if (! keyBoxR.isEmpty()) sections.push_back({ keyBoxR, "KEY" });
    sections.push_back({ voiceBoxR, name + " VOICE" });
    if (! filterBoxR.isEmpty()) sections.push_back({ filterBoxR, "FILTER" });
    sections.push_back({ insertBoxR, "INSERT" });
    sections.push_back({ grooveBoxR, rumble ? juce::String("ENVELOPE") : juce::String("LAYER") });
    if (! stepBoxR.isEmpty())  sections.push_back({ stepBoxR, "STEP " + juce::String(selectedStepOf[(size_t) currentLayer] + 1) });
    if (! stepsBoxR.isEmpty()) sections.push_back({ stepsBoxR, "STEPS" });
    MuLookAndFeel::drawSections(g, *this, sections, accent);

    // The shown track's colour on its voice box.
    g.setColour(layerColour());
    g.drawRoundedRectangle(voiceBoxR.toFloat().expanded(1.0f), 4.0f, 1.5f);

    // The KEY section is global (every track follows it): a pale double edge sets it apart from the per-track sections.
    if (! keyBoxR.isEmpty())
    {
        const auto edge = MuLookAndFeel::colour(MuLookAndFeel::labelText);
        g.setColour(edge.withAlpha(0.65f));
        g.drawRoundedRectangle(keyBoxR.toFloat().expanded(1.0f), 4.0f, 1.5f);
        g.setColour(edge.withAlpha(0.3f));
        g.drawRoundedRectangle(keyBoxR.toFloat().expanded(3.5f), 5.5f, 1.0f);
    }
}

void TrackContent::paintOverChildren(juce::Graphics& g)
{
    if (! MuLookAndFeel::hasScrews(*this)) return;
    for (auto r : { engineR, slotR })
        MuLookAndFeel::drawPanelScrews(g, r.reduced(2).toFloat());
}

} // namespace mu_on
