#include "UI/PhraseContent.h"

#include "Sequencer/Phrase.h"

#include <functional>

namespace mu_tilate
{

namespace
{
    using LF = MuLookAndFeel;
    constexpr int kStatusH = 22;   // the status line, the load button and the toolbar buttons (unscaled)
}

PhraseContent::PhraseContent(ProcessorBase& base, PhraseHost& h)
    : host(h), apvts(h.getPhraseParameters()), insertSub(base, "p")
{
    addAndMakeVisible(loadButton);
    loadButton.onClick = [this] { chooseLoop(); };

    status.setJustificationType(juce::Justification::centredLeft);
    status.setColour(juce::Label::textColourId, LF::colour(LF::labelText));
    addAndMakeVisible(status);

    for (int i = 0; i < tempoModeNames().size(); ++i) modeBox.addItem(tempoModeNames()[i], i + 1);
    addAndMakeVisible(modeBox);

    auto bpm = [](double v) { return juce::String(v, 1) + " BPM"; };
    auto db  = [](double v) { return v < 0.001 ? juce::String("-inf dB") : juce::String(juce::Decibels::gainToDecibels(v), 1) + " dB"; };
    addKnob(ppi::ltempo,   "Tempo",  LF::knobEuclidean,  "Loop Tempo: the tempo the loop was recorded at", bpm);
    addKnob(ppi::hold,     "Hold",   LF::knobPostPad,    "Transient Hold: how long each slice's attack plays unstretched", mu_fmt::Unit::Ms);
    addKnob(ppi::strength, "Str",    LF::knobPostPad,    "Strength: how much of the stretch is applied after the attack", mu_fmt::Unit::Percent01);
    addKnob(ppi::fadeIn,   "In",     LF::knobModulation, "Fade In: the fade at the start of each slice", mu_fmt::Unit::Ms);
    addKnob(ppi::fadeOut,  "Out",    LF::knobModulation, "Fade Out: the fade at the end of each slice", mu_fmt::Unit::Ms);
    addKnob(ppi::gain,     "Level",  LF::knobLevel,      "Level: the layer's output gain", db);

    // Detection: how the loop is cut into slices. Detect applies the settings (it replaces the slices and the timeline).
    for (int i = 0; i < detectMethodNames().size(); ++i) detectBox.addItem(detectMethodNames()[i], i + 1);
    addAndMakeVisible(detectBox);
    addKnob(ppi::dsens, "Sens", LF::knobEuclidean, "Sensitivity: how small a rise in the sound counts as a new hit", mu_fmt::Unit::Percent01);
    addKnob(ppi::dgap,  "Gap",  LF::knobEuclidean, "Min Gap: the closest two slice cuts may be", mu_fmt::Unit::Ms);
    addKnob(ppi::dpre,  "Pre",  LF::knobEuclidean, "Pre-roll: how far before the hit each cut is placed", mu_fmt::Unit::Ms);
    addKnob(ppi::dlow,  "Low",  LF::knobEuclidean, "Low Cut: sounds below this are ignored when looking for hits", mu_fmt::Unit::Hz);
    addKnob(ppi::dhigh, "High", LF::knobEuclidean, "High Cut: sounds above this are ignored when looking for hits", mu_fmt::Unit::Hz);
    // The layer's filter: every slice goes through it; a slice only offsets its cutoff and resonance.
    for (int i = 0; i < filterTypeNames().size(); ++i) filterTypeBox.addItem(filterTypeNames()[i], i + 1);
    filterTypeBox.getComboBox().setTooltip("Layer filter type: Off, low-pass, high-pass or band-pass. Slices move its cutoff and resonance with their own offsets");
    addAndMakeVisible(filterTypeBox);
    addKnob(ppi::fCut, "Cutoff", LF::knobPostPad, "Layer Filter Cutoff: the filter's corner frequency for the whole layer", mu_fmt::Unit::Hz);
    addKnob(ppi::fRes, "Res",    LF::knobPostPad, "Layer Filter Resonance: emphasis at the cutoff for the whole layer", mu_fmt::Unit::Percent01);

    insertSub.onStatusUpdate = [this](const juce::String& n, const juce::String& v) { if (onStatusUpdate) onStatusUpdate(n, v); };
    insertSub.onInsertAlgorithmChanged = [this](int algo) { if (onInsertAlgorithmChanged) onInsertAlgorithmChanged(algo); };
    addAndMakeVisible(insertSub);

    addAndMakeVisible(detectButton);
    detectButton.setTooltip("Detect again with these settings. Slices you placed or moved by hand stay; the others are replaced (settings stay with a cut that moved a little) and the timeline is laid out in source order.");
    detectButton.onClick = [this] { host.redetectSlices(currentLayer); };

    addAndMakeVisible(waveform);
    addAndMakeVisible(viewSwitch);
    viewSwitch.onChange = [this](int) { updateView(); };
    addChildComponent(slicePanel);
    slicePanel.setInterceptsMouseClicks(false, true);   // the layer's filter controls sit over its second row: only its own controls take clicks
    slicePanel.onStatusUpdate = [this](const juce::String& n, const juce::String& v) { if (onStatusUpdate) onStatusUpdate(n, v); };
    slicePanel.onChange = [this](const SliceSettings& settings)
    {
        const int idx = slicePanel.getSlice();
        // The panel edits the selected hit's own settings, or (no hit selected) the selected slice's, which new hits start with.
        if (shownPanelIsHit) editSlices([idx, settings](SliceList&, Timeline& tl) { tl.setSettings(idx, settings); });
        else                 editSlices([idx, settings](SliceList& sl, Timeline&) { sl.setSettings(idx, settings); });
        // The panel made this edit: it already shows it, so it must not write the knobs again while one is being dragged.
        if (const auto now = host.getPhrase(currentLayer).getData(); now != nullptr) shownPanelVersion = now->version;
    };
    slicePanel.onStep = [this](int step)
    {
        const auto d = host.getPhrase(currentLayer).getData();
        if (d == nullptr) return;
        if (shownPanelIsHit)   // step through the hits in time order
        {
            const int n = d->timeline.size();
            if (n == 0) return;
            timelineView.setSelectedBlock(juce::jlimit(0, n - 1, timelineView.getSelectedBlock() + step));
            if (const int b = timelineView.getSelectedBlock(); b >= 0) waveform.setSelectedRegion(d->timeline.blocks()[(size_t) b].slice);
            refreshSlicePanel(true);
            return;
        }
        if (d->slices.size() == 0) return;
        const int from = waveform.getSelectedRegion();
        selectRegion(from < 0 ? 0 : juce::jlimit(0, d->slices.size() - 1, from + step));
    };
    waveform.onRegionClicked = [this](int r) { selectRegion(r); };   // the slice a click on the timeline places
    waveform.onMarkerMoved = [this](int idx, int pos)
    {
        editSlices([idx, pos](SliceList& sl, Timeline&) { sl.moveMarker(idx, pos); });
        refreshViews(true);   // the marker follows the pointer at once, not at the next tick
    };
    waveform.onMarkerAddRequested = [this](int pos)
    {
        int at = -1;
        editSlices([&at, pos](SliceList& sl, Timeline& tl)
        {
            at = sl.addMarker(pos);
            if (at >= 0) tl.sliceInserted(at);
        });
        // The slices after the new one moved up by one: the selection moves with its slice.
        if (const int sel = waveform.getSelectedRegion(); at >= 0 && sel >= at) selectRegion(sel + 1);
        refreshViews(true);
    };
    waveform.onMarkerDeleteRequested = [this](int idx)
    {
        editSlices([idx](SliceList& sl, Timeline& tl) { if (sl.removeMarker(idx)) tl.sliceRemoved(idx); });
        selectRegion(-1);
    };

    // Dragging a slice from the waveform onto the timeline: the timeline shows where it would land, and a drop places a hit there.
    waveform.onRegionDragged = [this](int, juce::Point<int> screen)
    {
        timelineView.setDropBeat(timelineView.dropBeatAt(timelineView.getLocalPoint(nullptr, screen), juce::ModifierKeys::currentModifiers.isAltDown()));
    };
    waveform.onRegionDropped = [this](int region, juce::Point<int> screen)
    {
        const double beat = timelineView.dropBeatAt(timelineView.getLocalPoint(nullptr, screen), juce::ModifierKeys::currentModifiers.isAltDown());
        timelineView.setDropBeat(-1.0);
        if (beat >= 0.0) placeSliceAt(region, beat);
    };

    addAndMakeVisible(timelineView);
    timelineView.sliceToPlace = [this] { return waveform.getSelectedRegion(); };
    timelineView.settingsOfSlice = [this](int slice)
    {
        const auto d = host.getPhrase(currentLayer).getData();
        return d != nullptr && slice >= 0 && slice < d->slices.size() ? d->slices.settings(slice) : SliceSettings();
    };
    timelineView.onBlockSelected = [this](int block)   // selecting a hit selects its slice and shows the hit in the panel
    {
        const auto d = host.getPhrase(currentLayer).getData();
        if (d != nullptr && block >= 0 && block < d->timeline.size()) waveform.setSelectedRegion(d->timeline.blocks()[(size_t) block].slice);
        refreshSlicePanel(true);
        updateToolbar();
    };
    timelineView.onEdit = [this](const std::function<void(Timeline&)>& edit)
    {
        editSlices([&edit](SliceList&, Timeline& t) { edit(t); });
        refreshViews(true);   // the view shows the edit at once, not at the next tick
    };

    // The Sequencer's toolbar.
    lengthCaption.setJustificationType(juce::Justification::centredRight);
    lengthCaption.setColour(juce::Label::textColourId, LF::colour(LF::labelText));
    addAndMakeVisible(lengthCaption);
    lengthBox.setEditable(true);
    lengthBox.setJustificationType(juce::Justification::centred);
    lengthBox.setColour(juce::Label::backgroundColourId, LF::colour(LF::lfoEditorBackground));
    lengthBox.setColour(juce::Label::textColourId, LF::colour(LF::labelText));
    lengthBox.setTooltip("Sequence length: type bars, beats or a note value, e.g. 2 bars, 6 beats, 3/4");
    lengthBox.onTextChange = [this] { applyLength(lengthBox.getText()); };
    addAndMakeVisible(lengthBox);
    for (int i = 0; i < gridNames().size(); ++i) gridBox.addItem(gridNames()[i], i + 1);
    gridBox.getComboBox().setTooltip("Snap: the grid hits and their edges snap to (Alt while dragging is free)");
    addAndMakeVisible(gridBox);

    deleteButton.setTooltip("Delete the selected hit (or press Delete)");
    deleteButton.onClick = [this]
    {
        const int sel = timelineView.getSelectedBlock();
        if (sel < 0) return;
        timelineView.setSelectedBlock(-1);
        editSlices([sel](SliceList&, Timeline& t) { t.remove(sel); });
        refreshViews(true);
        refreshSlicePanel(true);
        updateToolbar();
    };
    clearButton.setTooltip("Clear the sequence: remove every hit and start again");
    clearButton.onClick = [this]
    {
        timelineView.setSelectedBlock(-1);
        editSlices([](SliceList&, Timeline& t) { t.clear(); });
        refreshViews(true);
        refreshSlicePanel(true);
        updateToolbar();
    };
    waveFitButton.setTooltip("Show the whole sample. The wheel zooms, Shift+wheel scrolls, a middle-button or Ctrl drag pans");
    waveFitButton.onClick = [this] { waveform.showAll(); };
    tlOutButton.setTooltip("Zoom the timeline out (the wheel zooms too)");
    tlOutButton.onClick = [this] { timelineView.zoomBy(1.0 / 1.5, timelineView.getWidth() / 2); };
    tlInButton.setTooltip("Zoom the timeline in (the wheel zooms too)");
    tlInButton.onClick = [this] { timelineView.zoomBy(1.5, timelineView.getWidth() / 2); };
    tlFitButton.setTooltip("Show the whole timeline. Shift+wheel scrolls, a drag on the ruler or a middle-button drag pans");
    tlFitButton.onClick = [this] { timelineView.showAll(); };
    for (auto* b : { &deleteButton, &clearButton, &waveFitButton, &tlOutButton, &tlInButton, &tlFitButton }) addAndMakeVisible(*b);

    updateView();
    startTimerHz(15);
}

// One edit: copy the layer's slices and timeline, apply the edit, publish the result (the audio is shared).
void PhraseContent::editSlices(const std::function<void(SliceList&, Timeline&)>& edit)
{
    auto& phrase = host.getPhrase(currentLayer);
    const auto d = phrase.getData();
    if (d == nullptr || d->audio == nullptr) return;
    SliceList slices = d->slices;
    Timeline timeline = d->timeline;
    edit(slices, timeline);
    phrase.commitEdit(slices, timeline);
}

void PhraseContent::selectRegion(int slice)
{
    timelineView.setSelectedBlock(-1);   // a slice is picked: no hit is selected, the panel edits the slice
    waveform.setSelectedRegion(slice);
    refreshSlicePanel(true);
    updateToolbar();
}

// A hit of `slice` at `beat` that starts with the slice's settings, then selected.
void PhraseContent::placeSliceAt(int slice, double beat)
{
    int at = -1;
    editSlices([&](SliceList& sl, Timeline& tl)
    {
        if (slice >= 0 && slice < sl.size()) at = tl.place(slice, beat, 0.0, sl.settings(slice));
    });
    refreshViews(true);
    if (at >= 0)
    {
        timelineView.setSelectedBlock(at);
        waveform.setSelectedRegion(slice);
    }
    refreshSlicePanel(true);
    updateToolbar();
}

// The length box: bars, beats or a note value. A bare number is in the unit the box was showing.
void PhraseContent::applyLength(const juce::String& text)
{
    const auto d = host.getPhrase(currentLayer).getData();
    const double current = d != nullptr ? d->timeline.lengthBeats() : 16.0;
    const bool wholeBars = std::abs(current / 4.0 - std::round(current / 4.0)) < 1.0e-6;
    const auto beats = parseLengthBeats(text, wholeBars ? 4.0 : 1.0);
    if (beats.has_value() && d != nullptr && d->audio != nullptr)
    {
        const double value = juce::jlimit(Timeline::kMinLengthBeats, Timeline::kMaxLengthBeats, *beats);
        editSlices([value](SliceList&, Timeline& t) { t.setLengthBeats(value); });
        if (auto* p = apvts.getParameter(layerParamPrefix(currentLayer) + ppi::suffix[ppi::bars]))   // the saved Length (bars) follows
            p->setValueNotifyingHost(p->convertTo0to1((float) juce::jlimit(1.0, 64.0, std::ceil(value / 4.0 - 1.0e-6))));
        if (onStatusUpdate) onStatusUpdate("Sequence Length", formatLengthBeats(value));
    }
    else if (onStatusUpdate)
        onStatusUpdate("Sequence Length", "not understood: type bars, beats or a note value, e.g. 2 bars, 6 beats, 3/4");
    shownLengthBeats = -1.0;   // show what the timeline has, whatever was typed
    refreshViews(true);
    updateToolbar();
}

void PhraseContent::updateToolbar()
{
    deleteButton.setEnabled(timelineView.getSelectedBlock() >= 0);
    const auto d = host.getPhrase(currentLayer).getData();
    clearButton.setEnabled(d != nullptr && d->timeline.size() > 0);
    if (d != nullptr && d->timeline.lengthBeats() != shownLengthBeats && ! lengthBox.isBeingEdited())
    {
        shownLengthBeats = d->timeline.lengthBeats();
        lengthBox.setText(formatLengthBeats(shownLengthBeats), juce::dontSendNotification);
    }
}

// Shows the Sequencer (waveform, toolbar, timeline) or the Sound Editor (panels, waveform, slim timeline, slice player).
void PhraseContent::updateView()
{
    const bool editor = soundEditorShown(), layerView = layerViewShown(), sequencer = ! editor && ! layerView;
    // Which panel a knob belongs to: the detector (Sound Editor), or the layer's sound and filter (Layer).
    for (auto& k : knobs) k.comp->setVisible(k.row >= ppi::dsens && k.row <= ppi::dhigh ? editor : layerView);
    modeBox.setVisible(layerView);
    filterTypeBox.setVisible(layerView);
    insertSub.setVisible(layerView);
    detectBox.setVisible(editor);
    detectButton.setVisible(editor);
    slicePanel.setVisible(editor);
    waveform.setVisible(! layerView);
    timelineView.setVisible(! layerView);
    for (auto* c : std::initializer_list<juce::Component*> { &lengthCaption, &lengthBox, &gridBox, &deleteButton, &clearButton,
                                                              &waveFitButton, &tlOutButton, &tlInButton, &tlFitButton })
        c->setVisible(sequencer);
    if (editor) refreshSlicePanel(true);
    updateToolbar();
    resized();
    repaint();
}

// The slice player shows the selected hit's own settings; with no hit selected, the selected slice's (what a new hit starts with).
void PhraseContent::refreshSlicePanel(bool force)
{
    const auto d = host.getPhrase(currentLayer).getData();
    const int hit = timelineView.getSelectedBlock();
    const bool isHit = d != nullptr && hit >= 0 && hit < d->timeline.size();
    const int sel = waveform.getSelectedRegion();
    if (! isHit && (d == nullptr || d->audio == nullptr || sel < 0 || sel >= d->slices.size()))
    {
        if (sel >= 0) waveform.setSelectedRegion(-1);   // the slices changed under the selection: nothing is selected now
        if (slicePanel.getSlice() != -1) slicePanel.setSlice(-1, {}, {});
        shownPanelIsHit = false;
        return;
    }
    const int target = isHit ? hit : sel;
    if (! force && target == slicePanel.getSlice() && isHit == shownPanelIsHit && d->version == shownPanelVersion) return;
    shownPanelVersion = d->version;
    shownPanelIsHit = isHit;
    if (isHit)
    {
        const auto& b = d->timeline.blocks()[(size_t) hit];
        slicePanel.setSlice(hit, b.own, "Hit " + juce::String(hit + 1) + " of " + juce::String(d->timeline.size()) + " at beat "
                                        + juce::String(b.start + 1.0, 2) + ", slice " + juce::String(b.slice + 1) + ". This hit has its own settings.");
        return;
    }
    const auto r = d->slices.slice(sel);
    slicePanel.setSlice(sel, d->slices.settings(sel),
                        "Slice " + juce::String(sel + 1) + " of " + juce::String(d->slices.size()) + "   "
                        + juce::String((double) r.start / d->fileSampleRate, 3) + " - " + juce::String((double) r.end / d->fileSampleRate, 3)
                        + " s.  New hits of this slice start with these settings; select a hit on the timeline to edit it.");
}

// Shows the layer's loop, slice markers and timeline; only reads what changed (the data pointer moves on every edit).
void PhraseContent::refreshViews(bool force)
{
    const auto d = host.getPhrase(currentLayer).getData();
    const double tempo = tempoParam != nullptr ? (double) tempoParam->load() : 120.0;
    if (gridParam != nullptr) timelineView.setGridBeats(gridBeats((int) gridParam->load()));
    const uint64_t version = d != nullptr ? d->version : 0;
    if (! force && version == shownVersion && tempo == shownTempo) return;
    shownVersion = version;
    shownTempo = tempo;
    if (d == nullptr || d->audio == nullptr)
    {
        shownAudioId = 0;
        waveform.setAudio(nullptr);
        waveform.setMarkers({});
        timelineView.setTimeline(Timeline(), {});
        updateToolbar();
        return;
    }

    // Each slice's own length in beats at the loop's tempo: the block widths the timeline draws.
    std::vector<double> beats;
    for (int i = 0; i < d->slices.size(); ++i)
        beats.push_back((double) d->slices.slice(i).length() / d->fileSampleRate * tempo / 60.0);
    timelineView.setTimeline(d->timeline, std::move(beats));
    if (d->audioId != shownAudioId) { shownAudioId = d->audioId; waveform.setAudio(d->audio); }
    std::vector<LoopWaveform::Marker> markers;
    for (int i = 0; i < d->slices.size(); ++i) markers.push_back({ d->slices.marker(i).position, d->slices.marker(i).pinned });
    waveform.setMarkers(std::move(markers));
    updateToolbar();
}

void PhraseContent::addKnob(int row, const char* label, MuLookAndFeel::ColourIds colour, const juce::String& statusName, mu_fmt::Unit unit)
{
    addKnob(row, label, colour, statusName, [unit](double v) { return mu_fmt::unitText(unit, v); });
}

void PhraseContent::addKnob(int row, const char* label, MuLookAndFeel::ColourIds colour, const juce::String& statusName,
                            std::function<juce::String(double)> format)
{
    KnobDef d;
    d.comp = std::make_unique<KnobWithLabel>(label, colour);
    d.row = row; d.name = ppi::suffix[row];
    d.comp->statusTextFromValue = std::move(format);
    d.comp->onStatusUpdate = [this, statusName](const juce::String&, const juce::String& v) { if (onStatusUpdate) onStatusUpdate(statusName, v); };
    addAndMakeVisible(*d.comp);
    knobs.push_back(std::move(d));
}

void PhraseContent::setLayer(int idx)
{
    currentLayer = juce::jmax(0, idx);
    const auto prefix = layerParamPrefix(currentLayer);

    // Drop every old binding first: a new attachment sets its control to the new layer's value, and a still-live old
    // one would write that value back into the previous layer.
    for (auto& k : knobs) k.att.reset();
    modeAtt.reset();
    detectAtt.reset();
    gridAtt.reset();
    filterTypeAtt.reset();
    insertSub.setChannel(currentLayer);

    for (auto& k : knobs)
        k.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, prefix + k.name, k.comp->getSlider());
    modeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(apvts, prefix + ppi::suffix[ppi::mode], modeBox.getComboBox());
    detectAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(apvts, prefix + ppi::suffix[ppi::dmethod], detectBox.getComboBox());
    filterTypeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(apvts, prefix + ppi::suffix[ppi::fType], filterTypeBox.getComboBox());
    gridAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(apvts, prefix + ppi::suffix[ppi::grid], gridBox.getComboBox());
    tempoParam = apvts.getRawParameterValue(prefix + ppi::suffix[ppi::ltempo]);   // read every tick: cached, not looked up by name
    gridParam  = apvts.getRawParameterValue(prefix + ppi::suffix[ppi::grid]);

    bindModulationArcs();
    shownVersion = ~(uint64_t) 0; shownAudioId = 0; shownPanelVersion = ~(uint64_t) 0; shownLengthBeats = -1.0;   // another layer's loop: show it afresh
    selectRegion(-1);
    timerCallback();
    repaint();
}

// Give every knob that has a row in the modulation target table its live modulation arc, driven by the current layer's
// matrix and the value the audio thread published for that target.
void PhraseContent::bindModulationArcs()
{
    const auto* matrix = &host.getPhrase(currentLayer).modulationMatrix;
    const int layer = currentLayer;

    for (auto& k : knobs)
    {
        const int dest = mu_mod::indexOfParam(kModDestTable, k.name.toStdString());
        if (dest < 0) { k.comp->clearModBinding(); continue; }
        k.comp->bindModulation(kModDestTable[dest].id, matrix, [this, layer, dest]
                               { return host.getModLive(layer, dest); });
    }
}

void PhraseContent::chooseLoop()
{
    chooser = std::make_unique<juce::FileChooser>("Load a loop", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    const int layer = currentLayer;   // the layer the dialog was opened for, not the one showing when it closes
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this, layer](const juce::FileChooser& fc)
                         {
                             const auto f = fc.getResult();
                             if (f.existsAsFile()) host.loadLoopAsync(layer, f);
                         });
}

void PhraseContent::timerCallback()
{
    const auto text = host.getLoopStatus(currentLayer);
    if (status.getText() != text) status.setText(text, juce::dontSendNotification);
    refreshViews();
    refreshSlicePanel(false);
    updateToolbar();
    timelineView.setPlayheadBeats(host.isPhraseTransportPlaying() ? host.getPhraseTransportBeat() : -1.0);
}

void PhraseContent::paint(juce::Graphics& g)
{
    MuLookAndFeel::drawAccentPanel(g, getLocalBounds().reduced(2).toFloat(), MuLookAndFeel::appAccent(*this));
    // The panels of the Sound Editor (the detector) and the Layer view (sound, filter, insert).
    if (! sections.empty()) LF::drawSections(g, *this, sections, LF::appAccent(*this));
}

void PhraseContent::resized()
{
    using mu_ui::s;
    auto area = getLocalBounds().reduced(s(LF::kPanelPad) * 2);

    // Top row: the load button, the status line and the view switch.
    auto top = area.removeFromTop(s(kStatusH));
    loadButton.setBounds(top.removeFromLeft(s(112)));
    top.removeFromLeft(s(LF::kSpaceS));
    viewSwitch.setBounds(top.removeFromRight(s(270)));
    top.removeFromRight(s(LF::kSpaceS));
    status.setBounds(top);
    area.removeFromTop(s(LF::kSpaceS));
    sections.clear();

    if (! soundEditorShown() && ! layerViewShown())
    {
        // Sequencer: the sample on top (to drag slices from), the toolbar, then the timeline.
        const int toolbarH = s(kStatusH) + s(LF::kSpaceS);
        const int timelineH = juce::jmax(s(90), area.getHeight() * 4 / 10);
        timelineView.setBounds(area.removeFromBottom(timelineH));
        auto bar = area.removeFromBottom(toolbarH);
        waveform.setBounds(area.withTrimmedBottom(s(LF::kSpaceS)));
        bar.removeFromTop(s(LF::kSpaceS) / 2);
        auto place = [&](juce::Component& c, int w) { c.setBounds(bar.removeFromLeft(s(w))); bar.removeFromLeft(s(LF::kSpaceS)); };
        place(waveFitButton, 84);
        bar.removeFromLeft(s(LF::kSpaceS) * 2);
        place(lengthCaption, 52);
        place(lengthBox, 90);
        place(gridBox, 78);
        place(deleteButton, 82);
        place(clearButton, 60);
        // The timeline's zoom sits at the right.
        tlFitButton.setBounds(bar.removeFromRight(s(100)));
        bar.removeFromRight(s(LF::kSpaceS));
        tlInButton.setBounds(bar.removeFromRight(s(30)));
        bar.removeFromRight(s(LF::kSpaceS));
        tlOutButton.setBounds(bar.removeFromRight(s(30)));
        slicePanel.setBounds({});
        return;
    }

    const int cellW = s(LF::kKnobSize2W), cellH = s(LF::kKnobSize2H), pad = s(LF::kSpaceS), plate = s(LF::kSectionPlateH);
    const int boxW = s(108), buttonW = s(64);
    auto isDetect = [](int row) { return row >= ppi::dsens && row <= ppi::dhigh; };
    auto isFilter = [](int row) { return row >= ppi::fType; };

    if (layerViewShown())
    {
        // Layer: the layer's sound, its filter and its insert effect, side by side, each in its own panel.
        int layerCells = 0, filterCells = 0;
        for (auto& k : knobs) { if (isDetect(k.row)) continue; (isFilter(k.row) ? filterCells : layerCells)++; }
        const int insertW = 4 * cellW + pad * 2, insertH = 2 * cellH + s(LF::kVoiceGap);
        const int soundW = pad * 3 + boxW + layerCells * cellW;
        const int filterW = pad * 3 + s(96) + filterCells * cellW;
        const int insertBoxW = insertW + pad * 2;
        const int boxH = std::max(insertH + pad * 2, cellH + pad * 2);
        const int y = area.getY() + plate;
        int x = area.getX();
        const juce::Rectangle<int> soundBox(x, y, soundW, boxH);        x += soundW + pad * 2;
        const juce::Rectangle<int> filterBox(x, y, filterW, boxH);      x += filterW + pad * 2;
        const juce::Rectangle<int> insertBox(x, y, insertBoxW, boxH);
        sections.push_back({ soundBox, "LAYER SOUND" });
        sections.push_back({ filterBox, "LAYER FILTER" });
        sections.push_back({ insertBox, "LAYER INSERT" });

        auto r = soundBox.reduced(pad, pad).removeFromTop(cellH);   // the controls sit in the top knob row of the (taller) boxes
        modeBox.setBounds(r.removeFromLeft(boxW).withSizeKeepingCentre(boxW, s(kStatusH)));
        r.removeFromLeft(pad);
        for (auto& k : knobs) if (! isDetect(k.row) && ! isFilter(k.row)) k.comp->setBounds(r.removeFromLeft(cellW).withHeight(cellH));
        auto f = filterBox.reduced(pad, pad).removeFromTop(cellH);
        filterTypeBox.setBounds(f.removeFromLeft(s(96)).withSizeKeepingCentre(s(96), s(kStatusH)));
        f.removeFromLeft(pad);
        for (auto& k : knobs) if (isFilter(k.row)) k.comp->setBounds(f.removeFromLeft(cellW).withHeight(cellH));
        insertSub.setBounds(insertBox.getX() + pad * 2, insertBox.getY() + pad, insertW, insertH);
        slicePanel.setBounds({});
        return;
    }

    // Sound Editor: the transient detector in its own panel, then the waveform, a slim timeline to pick a hit on, and the slice panels.
    int detectCells = 0;
    for (auto& k : knobs) if (isDetect(k.row)) ++detectCells;
    const int detectW = pad * 3 + boxW + detectCells * cellW + pad + buttonW;
    const int boxH = cellH + pad * 2;
    area.removeFromTop(plate);
    const juce::Rectangle<int> detectBoxArea(area.getX(), area.getY(), detectW, boxH);
    area.removeFromTop(boxH);
    sections.push_back({ detectBoxArea, "LAYER TRANSIENT DETECTOR" });
    {
        auto d = detectBoxArea.reduced(pad, pad);
        detectBox.setBounds(d.removeFromLeft(boxW).withSizeKeepingCentre(boxW, s(kStatusH)));
        d.removeFromLeft(pad);
        for (auto& k : knobs) if (isDetect(k.row)) k.comp->setBounds(d.removeFromLeft(cellW).withHeight(cellH));
        d.removeFromLeft(pad);
        detectButton.setBounds(d.removeFromLeft(buttonW).withSizeKeepingCentre(buttonW, s(kStatusH)));
    }
    area.removeFromTop(pad);

    // Below: the waveform, a slim timeline to pick a hit on, and the slice panels with what is left.
    const int sliceH = juce::jmax(s(190), area.getHeight() * 5 / 10);
    slicePanel.setBounds(area.removeFromBottom(sliceH));
    slicePanel.setRowIndent(0);
    area.removeFromBottom(pad);
    timelineView.setBounds(area.removeFromBottom(juce::jmax(s(56), area.getHeight() / 3)));
    area.removeFromBottom(pad);
    waveform.setBounds(area);
}

} // namespace mu_tilate
