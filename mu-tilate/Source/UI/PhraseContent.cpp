#include "UI/PhraseContent.h"

#include "Sequencer/Phrase.h"
#include "Analysis/OnsetDetector.h"

#include <functional>

namespace mu_tilate
{

namespace
{
    using LF = MuLookAndFeel;
    constexpr int kStatusH = LF::kLoopRowH;   // the status line, the load button and the toolbar buttons (unscaled)

    // Each slice's waveform as columns of peak level 0..1 (the loudest sample of the loop is 1), sampled sparsely: the timeline
    // draws them inside its blocks and under the pointer while a slice is dragged.
    std::vector<std::vector<float>> computeSlicePeaks(const juce::AudioBuffer<float>& audio, const SliceList& slices)
    {
        constexpr int kColumns = 96, kProbes = 24;
        std::vector<std::vector<float>> out;
        float loudest = 1.0e-6f;
        const int total = audio.getNumSamples(), channels = audio.getNumChannels();
        for (int i = 0; i < slices.size(); ++i)
        {
            const auto r = slices.slice(i);
            out.emplace_back((size_t) kColumns, 0.0f);
            const int len = r.length();
            if (len <= 0) continue;
            for (int col = 0; col < kColumns; ++col)
            {
                const int from = juce::jlimit(0, total, r.start + (int) ((int64_t) len * col / kColumns));
                const int to   = juce::jlimit(0, total, r.start + (int) ((int64_t) len * (col + 1) / kColumns));
                const int step = std::max(1, (to - from) / kProbes);
                float peak = 0.0f;
                for (int at = from; at < to; at += step)
                    for (int ch = 0; ch < channels; ++ch) peak = std::max(peak, std::abs(audio.getSample(ch, at)));
                out.back()[(size_t) col] = peak;
                loudest = std::max(loudest, peak);
            }
        }
        for (auto& v : out) for (auto& x : v) x /= loudest;
        return out;
    }

    juce::String signedText(double v, const juce::String& unit)
    {
        return (v > 0.0 ? "+" : "") + juce::String((int) std::lround(v)) + " " + unit;
    }
}

void PhraseContent::DragGhost::paint(juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced(0.5f);
    const auto accent = MuLookAndFeel::appAccent(*this);
    g.setColour(accent.withAlpha(0.55f));
    g.fillRoundedRectangle(r, 3.0f);
    g.setColour(accent);
    g.drawRoundedRectangle(r, 3.0f, 1.0f);
    if (peaks.empty() || getWidth() < 4) return;
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    const int n = (int) peaks.size(), mid = getHeight() / 2, half = getHeight() / 2 - 3;
    for (int x = 2; x < getWidth() - 2; ++x)
    {
        const int h = std::max(1, (int) std::lround(peaks[(size_t) std::min(n - 1, x * n / getWidth())] * (float) half));
        g.fillRect(x, mid - h, 1, h * 2);
    }
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
    auto semitones = [](double v) { return signedText(v, "st"); };
    auto cents = [](double v) { return signedText(v, "cents"); };
    auto panText = [](double v) { return std::abs(v) < 0.005 ? juce::String("Centre") : (v < 0.0 ? "L " : "R ") + juce::String((int) std::lround(std::abs(v) * 100.0)) + "%"; };
    auto signedPercent = [](double v) { return (v > 0.0 ? "+" : "") + juce::String((int) std::lround(v * 100.0)) + "%"; };

    // The layer's sound strip.
    addKnob(ppi::ltempo,   "Tempo",  LF::knobEuclidean,  "Loop Tempo: the tempo the loop was recorded at", bpm);
    addKnob(ppi::tmix,     "Pitch/Str", LF::knobPostPad, "Tempo match: 0 % lets the pitch follow the tempo, 100 % time-stretches and keeps the pitch", mu_fmt::Unit::Percent01);
    addKnob(ppi::hold,     "Hold",   LF::knobPostPad,    "Transient Hold: how long each slice's attack plays unstretched", mu_fmt::Unit::Ms);
    addKnob(ppi::strength, "Str",    LF::knobPostPad,    "Strength: how much of the stretch is applied after the attack", mu_fmt::Unit::Percent01);

    // The layer's voice band: Pitch | Filter | Amp, absolute values (a slice adds its own offsets in the Slice view).
    addKnob(ppi::pSemi, "Pitch", LF::knobEuclidean, "Layer Pitch: transposes every slice", semitones);
    addKnob(ppi::pFine, "Fine",  LF::knobEuclidean, "Layer Fine Tune: fine pitch of every slice", cents);
    for (int i = 0; i < filterTypeNames().size(); ++i) filterTypeBox.addItem(filterTypeNames()[i], i + 1);
    filterTypeBox.getComboBox().setTooltip("Layer filter type: Off or one of the family's sixteen. Slices move its cutoff and resonance with their own offsets");
    addKnob(ppi::fCut,  "Cutoff", LF::knobPostPad, "Layer Filter Cutoff: the filter's corner frequency for the whole layer", mu_fmt::Unit::Hz);
    addKnob(ppi::fRes,  "Res",    LF::knobPostPad, "Layer Filter Resonance: emphasis at the cutoff for the whole layer", mu_fmt::Unit::Percent01);
    addKnob(ppi::fEnv,  "Depth",  LF::knobPostPad, "Layer Filter Envelope Depth: how far the filter envelope moves the cutoff", signedPercent);
    addKnob(ppi::fltA,  "A", LF::knobPostPad, "Layer Filter Attack: time for the filter envelope to reach its peak", mu_fmt::Unit::Ms);
    addKnob(ppi::fltD,  "D", LF::knobPostPad, "Layer Filter Decay: time to fall to the sustain level", mu_fmt::Unit::Ms);
    addKnob(ppi::fltS,  "S", LF::knobPostPad, "Layer Filter Sustain: the envelope level held after the decay", mu_fmt::Unit::Percent01);
    addKnob(ppi::fltR,  "R", LF::knobPostPad, "Layer Filter Release: how long the envelope takes to close when a slice is cut", mu_fmt::Unit::Ms);
    addKnob(ppi::gain,    "Level", LF::knobLevel,      "Level: the layer's output gain", db);
    addKnob(ppi::pPan,    "Pan",   LF::knobPan,        "Layer Pan: where the layer sits in the stereo field", panText);
    addKnob(ppi::fadeIn,  "In",    LF::knobModulation, "Fade In: the fade at the start of each slice", mu_fmt::Unit::Ms);
    addKnob(ppi::fadeOut, "Out",   LF::knobModulation, "Fade Out: the fade at the end of each slice", mu_fmt::Unit::Ms);
    addKnob(ppi::ampA,  "A", LF::knobLevel, "Layer Amp Attack: time for each slice to reach full level", mu_fmt::Unit::Ms);
    addKnob(ppi::ampD,  "D", LF::knobLevel, "Layer Amp Decay: time to fall to the sustain level", mu_fmt::Unit::Ms);
    addKnob(ppi::ampS,  "S", LF::knobLevel, "Layer Amp Sustain: the level held after the decay", mu_fmt::Unit::Percent01);
    addKnob(ppi::ampR,  "R", LF::knobLevel, "Layer Amp Release: fade-out time when a slice is cut", mu_fmt::Unit::Ms);

    layerPitchSection.place(knobOf(ppi::pSemi), 0, 0);
    layerPitchSection.place(knobOf(ppi::pFine), 0, 1);
    layerFilterSection.place(filterTypeBox, 0, 0, 2, true);
    layerFilterSection.place(knobOf(ppi::fCut), 0, 2);
    layerFilterSection.place(knobOf(ppi::fRes), 0, 3);
    layerFilterSection.place(knobOf(ppi::fEnv), 0, 4);
    layerFilterSection.place(knobOf(ppi::fltA), 1, 0);
    layerFilterSection.place(knobOf(ppi::fltD), 1, 1);
    layerFilterSection.place(knobOf(ppi::fltS), 1, 2);
    layerFilterSection.place(knobOf(ppi::fltR), 1, 3);
    layerAmpSection.place(knobOf(ppi::gain), 0, 0);
    layerAmpSection.place(knobOf(ppi::pPan), 0, 1);
    layerAmpSection.place(knobOf(ppi::fadeIn), 0, 2);
    layerAmpSection.place(knobOf(ppi::fadeOut), 0, 3);
    layerAmpSection.place(knobOf(ppi::ampA), 1, 0);
    layerAmpSection.place(knobOf(ppi::ampD), 1, 1);
    layerAmpSection.place(knobOf(ppi::ampS), 1, 2);
    layerAmpSection.place(knobOf(ppi::ampR), 1, 3);

    insertSub.onStatusUpdate = [this](const juce::String& n, const juce::String& v) { if (onStatusUpdate) onStatusUpdate(n, v); };
    insertSub.onInsertAlgorithmChanged = [this](int algo)
    {
        slicePanel.setLayerInsertAlgorithm(algo);
        if (onInsertAlgorithmChanged) onInsertAlgorithmChanged(algo);
    };
    layerBand.setTitlePrefix("LAYER");
    layerBand.setSections(layerPitchSection, layerFilterSection, layerAmpSection, insertSub, { nullptr, nullptr, nullptr });
    addAndMakeVisible(layerBand);

    // Detection: how the loop is cut into slices. Detect applies the settings (it replaces the slices and the timeline).
    for (int i = 0; i < detectMethodNames().size(); ++i) detectBox.addItem(detectMethodNames()[i], i + 1);
    addAndMakeVisible(detectBox);
    addKnob(ppi::dsens, "Sens", LF::knobEuclidean, "Sensitivity: how small a rise in the sound counts as a new hit", mu_fmt::Unit::Percent01);
    addKnob(ppi::dgap,  "Gap",  LF::knobEuclidean, "Min Gap: the closest two slice cuts may be", mu_fmt::Unit::Ms);
    addKnob(ppi::dpre,  "Pre",  LF::knobEuclidean, "Pre-roll: how far before the hit each cut is placed", mu_fmt::Unit::Ms);
    addKnob(ppi::dlow,  "Low",  LF::knobEuclidean, "Low Cut: sounds below this are ignored when looking for hits", mu_fmt::Unit::Hz);
    addKnob(ppi::dhigh, "High", LF::knobEuclidean, "High Cut: sounds above this are ignored when looking for hits", mu_fmt::Unit::Hz);

    addAndMakeVisible(detectButton);
    detectButton.setTooltip("Detect again with these settings. Slices you placed or moved by hand stay; the others are replaced (settings stay with a cut that moved a little) and the timeline is laid out in source order.");
    detectButton.onClick = [this] { host.redetectSlices(currentLayer); };

    addAndMakeVisible(waveform);
    addAndMakeVisible(viewSwitch);
    viewSwitch.onChange = [this](int) { updateView(); };
    addChildComponent(slicePanel);
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
    // A double-click adds a marker on the nearest attack.
    waveform.snapNewMarker = [this](int pos)
    {
        const auto d = host.getPhrase(currentLayer).getData();
        return d != nullptr && d->audio != nullptr ? mu_core::nearestOnset(*d->audio, d->fileSampleRate, pos) : pos;
    };
    waveform.onRegionRenamed = [this](int region, const juce::String& name)
    {
        editSlices([region, name](SliceList& sl, Timeline&) { sl.setName(region, name); });
        refreshViews(true);
    };
    waveform.onMarkerDeleteRequested = [this](int idx)
    {
        editSlices([idx](SliceList& sl, Timeline& tl) { if (sl.removeMarker(idx)) tl.sliceRemoved(idx); });
        selectRegion(-1);
    };

    // Dragging a slice from the waveform onto the timeline: its waveform follows the pointer, then shows on the timeline where a drop would
    // place the hit.
    addChildComponent(dragGhost);
    dragGhost.setInterceptsMouseClicks(false, false);
    waveform.onRegionDragged = [this](int region, juce::Point<int> screen)
    {
        using mu_ui::s;
        const double beat = timelineView.dropBeatAt(timelineView.getLocalPoint(nullptr, screen), juce::ModifierKeys::currentModifiers.isShiftDown());
        timelineView.setDropBeat(beat);
        if (draggedSlice != region)
        {
            draggedSlice = region;
            dragGhost.peaks = region >= 0 && region < (int) slicePeaks.size() ? slicePeaks[(size_t) region] : std::vector<float>();
            timelineView.setDropSlice(region);
        }
        // Over the timeline its own preview shows the landing place; elsewhere the preview follows the pointer.
        const bool overTimeline = beat >= 0.0;
        dragGhost.setVisible(! overTimeline);
        if (! overTimeline)
        {
            const int w = juce::jlimit(s(24), s(360), timelineView.pixelsForSlice(region)), h = s(LF::kLoopGhostH);
            dragGhost.setBounds(juce::Rectangle<int>(w, h).withCentre(getLocalPoint(nullptr, screen)));
            dragGhost.toFront(false);
            dragGhost.repaint();
        }
    };
    waveform.onRegionDropped = [this](int region, juce::Point<int> screen)
    {
        const double beat = timelineView.dropBeatAt(timelineView.getLocalPoint(nullptr, screen), juce::ModifierKeys::currentModifiers.isShiftDown());
        timelineView.setDropBeat(-1.0);
        timelineView.setDropSlice(-1);
        dragGhost.setVisible(false);
        draggedSlice = -1;
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
    for (auto* n : { &barsInput, &beatsInput })
    {
        n->setShowStepButtons(false);
        n->setLabelInline(true);
        n->onChange = [this](int) { applyLength(); };
        addAndMakeVisible(*n);
    }
    for (int i = 0; i < gridNames().size(); ++i) gridBox.addItem(gridNames()[i], i + 1);
    gridBox.getComboBox().setTooltip("Snap: the grid hits and their edges snap to (Shift while dragging is free)");
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
    layOutButton.setTooltip("Lay the slices out one after another in their original order from the start (replaces the hits and sizes the sequence to fit)");
    layOutButton.onClick = [this]
    {
        timelineView.setSelectedBlock(-1);
        host.layOutSlices(currentLayer);
        shownLengthBeats = -1.0;
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
    rangeBar.setTooltip("The whole sample: drag the ends to use only a section of it (the file is not changed), drag inside to slide the section");
    rangeBar.onRangeChanged = [this](int start, int end) { timelineView.setSelectedBlock(-1); host.setSourceRegion(currentLayer, start, end); };
    addAndMakeVisible(rangeBar);
    previewStopButton.setTooltip("Stop the audition");
    previewTempoButton.setTooltip("Play the loop at the project tempo, as the layer's Tempo Match and Pitch/Stretch set it");
    previewOriginalButton.setTooltip("Play the used section of the sample as it is, at its own tempo");
    previewSliceButton.setTooltip("Play the selected slice");
    previewTempoButton.onClick    = [this] { host.previewSource(currentLayer, 1, 0); };
    previewOriginalButton.onClick = [this] { host.previewSource(currentLayer, 0, 0); };
    previewSliceButton.onClick    = [this] { host.previewSource(currentLayer, 2, juce::jmax(0, waveform.getSelectedRegion())); };
    previewStopButton.onClick     = [this] { host.stopPreview(currentLayer); };
    previewLoopButton.setClickingTogglesState(true);
    previewLoopButton.setTooltip("Repeat the whole-loop auditions until stopped (a single slice plays once)");
    previewLoopButton.onClick     = [this] { host.setPreviewLooping(currentLayer, previewLoopButton.getToggleState()); };
    waveform.onRegionPlayRequested = [this](int r)
    {
        selectRegion(r);
        host.previewSource(currentLayer, 2, r);
    };
    // A marker dragged close to a grid line (of the loop, at the loop's tempo) jumps onto it; Shift moves it freely.
    waveform.snapMarker = [this](int position, double samplesPerPixel)
    {
        const auto d = host.getPhrase(currentLayer).getData();
        const double grid = gridParam != nullptr ? gridBeats((int) gridParam->load()) : 0.0;
        const double tempo = tempoParam != nullptr ? (double) tempoParam->load() : 0.0;
        if (d == nullptr || grid <= 0.0 || tempo <= 0.0) return position;
        const double step = grid * 60.0 / tempo * d->fileSampleRate;   // samples between grid lines
        const double nearest = std::round((double) position / step) * step;
        return std::abs(nearest - (double) position) <= (double) mu_ui::s(LF::kMarkerSnapPx) * samplesPerPixel ? (int) std::lround(nearest) : position;
    };
    // The play area: the handles on the waveform bound what the whole-loop auditions play; Crop cuts the sample to it. Slices and hits
    // stay; a hit whose slice is cut off is dimmed on the timeline and silent.
    waveform.onPlayRangeChanged = [this](int start, int end)
    {
        host.setPlayArea(currentLayer, start, end);
        cropButton.setEnabled(! waveform.playRangeIsAll());
    };
    cropButton.setEnabled(false);
    cropButton.setTooltip("Crop the sample to the play area between the two arrows. Slices and hits stay; a hit whose slice is cut off is dimmed and silent. The range bar can widen the section again");
    cropButton.onClick = [this]
    {
        const int start = waveform.getPlayStart(), end = waveform.getPlayEnd();
        timelineView.setSelectedBlock(-1);
        host.cropLoop(currentLayer, start, end);
        cropButton.setEnabled(false);
        refreshViews(true);
    };
    waveFitButton.onClick = [this] { waveform.showAll(); };
    waveOutButton.onClick = [this] { waveform.zoomBy(0.5, waveform.getWidth() / 2); };
    waveInButton.onClick  = [this] { waveform.zoomBy(2.0, waveform.getWidth() / 2); };
    waveOutButton.setTooltip("Zoom the sample out");
    waveInButton.setTooltip("Zoom the sample in (the mouse wheel zooms around the pointer, Shift+wheel scrolls)");
    tlOutButton.setTooltip("Zoom the timeline out (the wheel zooms too)");
    tlOutButton.onClick = [this] { timelineView.zoomBy(1.0 / 1.5, timelineView.getWidth() / 2); };
    tlInButton.setTooltip("Zoom the timeline in (the wheel zooms too)");
    tlInButton.onClick = [this] { timelineView.zoomBy(1.5, timelineView.getWidth() / 2); };
    tlFitButton.setTooltip("Show the whole timeline. Shift+wheel scrolls, a drag on the ruler or a middle-button drag pans");
    tlFitButton.onClick = [this] { timelineView.showAll(); };
    for (auto* b : std::initializer_list<juce::Component*> { &deleteButton, &clearButton, &layOutButton, &previewTempoButton, &previewOriginalButton, &previewSliceButton, &previewLoopButton, &previewStopButton, &cropButton, &waveFitButton, &waveOutButton, &waveInButton, &tlOutButton, &tlInButton, &tlFitButton }) addAndMakeVisible(*b);

    updateView();
    startTimerHz(30);
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

// The Bars and Beats toggles set the length: bars * 4 + beats, at least one beat.
void PhraseContent::applyLength()
{
    const auto d = host.getPhrase(currentLayer).getData();
    if (d != nullptr && d->audio != nullptr)
    {
        const double value = juce::jlimit(Timeline::kMinLengthBeats, Timeline::kMaxLengthBeats,
                                          (double) barsInput.getValue() * 4.0 + (double) beatsInput.getValue());
        editSlices([value](SliceList&, Timeline& t) { t.setLengthBeats(value); });
        if (onStatusUpdate) onStatusUpdate("Sequence Length", formatLengthBeats(value));
    }
    shownLengthBeats = -1.0;   // show what the timeline has, whatever was typed
    refreshViews(true);
    updateToolbar();
}

void PhraseContent::updateToolbar()
{
    deleteButton.setEnabled(timelineView.getSelectedBlock() >= 0);
    const auto d = host.getPhrase(currentLayer).getData();
    clearButton.setEnabled(d != nullptr && d->timeline.size() > 0);
    layOutButton.setEnabled(d != nullptr && d->audio != nullptr && d->slices.size() > 0);
    if (d != nullptr && d->timeline.lengthBeats() != shownLengthBeats)
    {
        shownLengthBeats = d->timeline.lengthBeats();
        const int whole = (int) std::lround(shownLengthBeats);
        barsInput.setValue(whole / 4);
        beatsInput.setValue(whole % 4);
    }
}

// Shows the Sequencer (waveform, toolbar, timeline), the Layer view (sound strip and voice band) or the Slice view (detector,
// waveform, slim timeline, slice panel).
void PhraseContent::updateView()
{
    const bool layerView = layerViewShown(), sliceView = sliceViewShown(), sequencer = ! layerView && ! sliceView;
    // Which view a knob belongs to: the detector's are in the Sequencer view beside the waveform, the sound strip's in the Layer view; the voice band's
    // knobs follow the band.
    for (auto& k : knobs)
    {
        if (k.row >= ppi::dsens && k.row <= ppi::dhigh) k.comp->setVisible(sequencer);
        else if (k.row == ppi::ltempo || k.row == ppi::tmix || k.row == ppi::hold || k.row == ppi::strength) k.comp->setVisible(sequencer);
    }
    modeBox.setVisible(sequencer);
    layerBand.setVisible(layerView);
    detectBox.setVisible(sequencer);
    detectButton.setVisible(sequencer);
    slicePanel.setVisible(sliceView);
    waveform.setVisible(! layerView);
    rangeBar.setVisible(sequencer);
    timelineView.setVisible(! layerView);
    for (auto* c : std::initializer_list<juce::Component*> { &lengthCaption, &barsInput, &beatsInput, &gridBox, &layOutButton, &deleteButton, &clearButton,
                                                              &waveFitButton, &waveOutButton, &waveInButton, &previewTempoButton, &previewOriginalButton, &previewSliceButton, &previewLoopButton, &previewStopButton, &cropButton, &tlOutButton, &tlInButton, &tlFitButton })
        c->setVisible(sequencer);
    if (sliceView) refreshSlicePanel(true);
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
                        d->slices.displayName(sel) + " (" + juce::String(sel + 1) + " of " + juce::String(d->slices.size()) + ")   "
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
        rangeBar.setAudio(nullptr);
        waveform.setMarkers({});
        timelineView.setTimeline(Timeline(), {});
        slicePeaks.clear();
        timelineView.setSlicePeaks({});
        updateToolbar();
        return;
    }

    // Each slice's own length in beats at the loop's tempo: the block widths the timeline draws.
    std::vector<double> beats;
    for (int i = 0; i < d->slices.size(); ++i)
        beats.push_back((double) d->slices.slice(i).length() / d->fileSampleRate * tempo / 60.0);
    timelineView.setTimeline(d->timeline, std::move(beats));
    slicePeaks = computeSlicePeaks(*d->audio, d->slices);
    timelineView.setSlicePeaks(slicePeaks);
    {
        // The whole loop's peaks, and each slice's place in it.
        const int total = juce::jmax(1, d->audio->getNumSamples());
        constexpr int kColumns = 2048, kProbes = 16;
        std::vector<float> loop((size_t) kColumns, 0.0f);
        float loudest = 1.0e-6f;
        for (int col = 0; col < kColumns; ++col)
        {
            const int from = (int) ((int64_t) total * col / kColumns), to = (int) ((int64_t) total * (col + 1) / kColumns);
            const int step = std::max(1, (to - from) / kProbes);
            float peak = 0.0f;
            for (int at = from; at < to; at += step)
                for (int ch = 0; ch < d->audio->getNumChannels(); ++ch) peak = std::max(peak, std::abs(d->audio->getSample(ch, at)));
            loop[(size_t) col] = peak;
            loudest = std::max(loudest, peak);
        }
        for (auto& v : loop) v /= loudest;
        std::vector<PhraseTimeline::SliceSpan> spans;
        for (int i = 0; i < d->slices.size(); ++i)
        {
            const auto r = d->slices.slice(i);
            spans.push_back({ (double) r.start / total, (double) r.length() / total });
        }
        timelineView.setLoopPeaks(std::move(loop), std::move(spans));
    }
    if (d->audioId != shownAudioId)
    {
        shownAudioId = d->audioId;
        waveform.setAudio(d->audio);
        rangeBar.setAudio(d->source != nullptr ? d->source : d->audio);
        rangeBar.setRange(d->regionStart, d->regionEnd);
        host.setPlayArea(currentLayer, 0, 0);   // a new loop plays all of itself (the waveform reset its handles)
        cropButton.setEnabled(false);
    }
    std::vector<LoopWaveform::Marker> markers;
    for (int i = 0; i < d->slices.size(); ++i) markers.push_back({ d->slices.marker(i).position, d->slices.marker(i).pinned, d->slices.marker(i).name });
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

KnobWithLabel& PhraseContent::knobOf(int row)
{
    for (auto& k : knobs)
        if (k.row == row) return *k.comp;
    jassertfalse;
    return *knobs.front().comp;
}

// Tells the slice panel which filter type and insert effect the layer has now (a preset or the Layer view may change them).
void PhraseContent::followLayerChoices()
{
    if (filterTypeParam != nullptr)
        if (const int v = (int) filterTypeParam->load(); v != shownFilterType) { shownFilterType = v; slicePanel.setLayerFilterType(v); }
    if (insertAlgoParam != nullptr)
        if (const int v = (int) insertAlgoParam->load(); v != shownInsertAlgo) { shownInsertAlgo = v; slicePanel.setLayerInsertAlgorithm(v); }
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
    filterTypeParam = apvts.getRawParameterValue(prefix + ppi::suffix[ppi::fType]);
    insertAlgoParam = apvts.getRawParameterValue(prefix + ppi::suffix[ppi::drvChar]);
    shownFilterType = shownInsertAlgo = -1;
    followLayerChoices();

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
    followLayerChoices();
    updateToolbar();
    waveform.setPlayhead(host.getPreviewPlayhead(currentLayer));
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
    loadButton.setBounds(top.removeFromLeft(s(LF::kLoopLoadW)));
    top.removeFromLeft(s(LF::kSpaceS));
    viewSwitch.setBounds(top.removeFromRight(s(LF::kLoopViewSwitchW)));
    top.removeFromRight(s(LF::kSpaceS));
    status.setBounds(top);
    area.removeFromTop(s(LF::kSpaceS));
    sections.clear();

    const int cellW = s(LF::kKnobSize2W), cellH = s(LF::kKnobSize2H), pad = s(LF::kSpaceS), plate = s(LF::kSectionPlateH);
    const int boxW = s(LF::kLoopBoxW), buttonW = s(LF::kLoopButtonW);
    auto isDetect = [](int row) { return row >= ppi::dsens && row <= ppi::dhigh; };

    if (! sliceViewShown() && ! layerViewShown())
    {
        // Sequencer: the sample on top (to drag slices from), the toolbar, then the timeline.
        const int toolbarH = s(kStatusH) + s(LF::kSpaceS);
        const int timelineH = juce::jmax(s(90), area.getHeight() * 4 / 10);
        timelineView.setBounds(area.removeFromBottom(timelineH));
        auto bar = area.removeFromBottom(toolbarH);
        // The transient detector and the audition controls, each in its own panel under a plate: side by side, or stacked when narrow.
        {
            const int padS = s(LF::kSpaceS);
            int detectCells = 0;
            for (auto& k : knobs) if (isDetect(k.row)) ++detectCells;
            const int detectW = pad * 3 + boxW + detectCells * cellW + pad + buttonW;
            const int detectH = cellH + pad * 2;
            const int soundW = pad * 3 + boxW + 4 * cellW;
            const int playW[] = { s(80), s(92), s(74) }, iconW = s(32);
            const int previewW = padS * 2 + playW[0] + playW[1] + playW[2] + 2 * iconW + 4 * padS;
            const int previewH = s(kStatusH) + padS * 2;

            // The detector, the layer's sound strip and the audition controls, each in its own panel under a plate: left to right, wrapping
            // onto a new row when the next one does not fit.
            int x = area.getX(), rowTop = area.getY() + plate;
            auto nextPanel = [&](int w, int h)
            {
                if (x > area.getX() && x + w > area.getRight()) { x = area.getX(); rowTop += detectH + padS + plate; }
                const juce::Rectangle<int> r(x, rowTop, w, h);
                x += w + padS * 2;
                return r;
            };

            const auto detectBoxArea = nextPanel(detectW, detectH);
            sections.push_back({ detectBoxArea, "LAYER TRANSIENT DETECTOR" });
            {
                auto d = detectBoxArea.reduced(pad, pad);
                detectBox.setBounds(d.removeFromLeft(boxW).withSizeKeepingCentre(boxW, s(kStatusH)));
                d.removeFromLeft(pad);
                for (auto& k : knobs) if (isDetect(k.row)) k.comp->setBounds(d.removeFromLeft(cellW).withHeight(cellH));
                d.removeFromLeft(pad);
                detectButton.setBounds(d.removeFromLeft(buttonW).withSizeKeepingCentre(buttonW, s(kStatusH)));
            }

            // The sound strip: tempo match, tempo, pitch/stretch, hold, strength.
            const auto soundBox = nextPanel(soundW, detectH);
            sections.push_back({ soundBox, "LAYER SOUND" });
            {
                auto r = soundBox.reduced(pad, pad);
                modeBox.setBounds(r.removeFromLeft(boxW).withSizeKeepingCentre(boxW, s(kStatusH)));
                r.removeFromLeft(pad);
                for (int row : { (int) ppi::ltempo, (int) ppi::tmix, (int) ppi::hold, (int) ppi::strength })
                    knobOf(row).setBounds(r.removeFromLeft(cellW).withHeight(cellH));
            }

            // The audition: three plays, stop and repeat as small icon buttons.
            const auto box = nextPanel(previewW, previewH);
            sections.push_back({ box, "PREVIEW" });
            {
                auto row = box.reduced(padS);
                int k = 0;
                for (auto* b : std::initializer_list<juce::Component*> { &previewTempoButton, &previewOriginalButton, &previewSliceButton, &previewStopButton, &previewLoopButton })
                {
                    b->setBounds(row.removeFromLeft(k < 3 ? playW[k] : iconW));
                    row.removeFromLeft(padS);
                    ++k;
                }
            }
            area.removeFromTop(rowTop + detectH - area.getY());
            area.removeFromTop(padS);
        }
        rangeBar.setBounds(area.removeFromTop(s(40)));
        area.removeFromTop(s(LF::kSpaceS));
        waveform.setBounds(area.withTrimmedBottom(s(LF::kSpaceS)));
        bar.removeFromTop(s(LF::kSpaceS) / 2);
        auto place = [&](juce::Component& c, int w) { c.setBounds(bar.removeFromLeft(s(w))); bar.removeFromLeft(s(LF::kSpaceS)); };
        place(waveFitButton, 84);
        place(waveOutButton, 30);
        place(waveInButton, 30);
        place(cropButton, 60);
        bar.removeFromLeft(s(LF::kSpaceS) * 2);
        place(lengthCaption, 50);
        place(barsInput, 104);
        place(beatsInput, 108);
        place(gridBox, 78);
        place(layOutButton, 72);
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

    if (layerViewShown())
    {
        // Layer: the voice band: Pitch | Filter | Amp | Effects (the tempo-match strip is on the Sequencer view).
        layerBand.setBounds(area.getX(), area.getY() + plate, s(VoiceBand::kEffectsX + VoiceBand::kEffectsW), s(VoiceBand::kHeight));
        slicePanel.setBounds({});
        return;
    }

    // Slice view: the waveform, a slim timeline to pick a hit on, and the slice panel.
    slicePanel.setBounds(area.removeFromBottom(s(SlicePanel::preferredHeight())));
    area.removeFromBottom(pad);
    timelineView.setBounds(area.removeFromBottom(s(LF::kLoopSlimTimelineH)));
    area.removeFromBottom(pad);
    waveform.setBounds(area);
}

} // namespace mu_tilate
