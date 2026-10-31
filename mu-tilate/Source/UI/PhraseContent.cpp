#include "UI/PhraseContent.h"

#include "Sequencer/Phrase.h"

#include <functional>

namespace mu_tilate
{

namespace
{
    using LF = MuLookAndFeel;
    constexpr int kStatusH = 22;   // the status line and the load button's height (unscaled)
}

PhraseContent::PhraseContent(PhraseHost& h)
    : host(h), apvts(h.getPhraseParameters())
{
    addAndMakeVisible(loadButton);
    loadButton.onClick = [this] { chooseLoop(); };

    status.setJustificationType(juce::Justification::centredLeft);
    status.setColour(juce::Label::textColourId, LF::colour(LF::labelText));
    addAndMakeVisible(status);

    for (int i = 0; i < tempoModeNames().size(); ++i) modeBox.addItem(tempoModeNames()[i], i + 1);
    addAndMakeVisible(modeBox);

    addKnob(ppi::ltempo,   "Tempo",  LF::knobEuclidean);
    addKnob(ppi::hold,     "Hold",   LF::knobPostPad);
    addKnob(ppi::strength, "Str",    LF::knobPostPad);
    addKnob(ppi::fadeIn,   "In",     LF::knobModulation);
    addKnob(ppi::fadeOut,  "Out",    LF::knobModulation);
    addKnob(ppi::gain,     "Level",  LF::knobLevel);

    addAndMakeVisible(waveform);
    waveform.onRegionClicked = [this](int r) { waveform.setSelectedRegion(r); };   // the slice a click on the timeline places
    waveform.onMarkerMoved = [this](int idx, int pos)
    {
        editSlices([idx, pos](SliceList& sl, Timeline&) { sl.moveMarker(idx, pos); });
    };
    waveform.onMarkerAddRequested = [this](int pos)
    {
        editSlices([pos](SliceList& sl, Timeline& tl)
        {
            const int at = sl.addMarker(pos);
            if (at >= 0) tl.sliceInserted(at);
        });
    };
    waveform.onMarkerDeleteRequested = [this](int idx)
    {
        editSlices([idx](SliceList& sl, Timeline& tl) { if (sl.removeMarker(idx)) tl.sliceRemoved(idx); });
        waveform.setSelectedRegion(-1);
    };

    addAndMakeVisible(timelineView);
    timelineView.sliceToPlace = [this] { return waveform.getSelectedRegion(); };
    timelineView.onEdit = [this](const std::function<void(Timeline&)>& edit)
    {
        editSlices([&edit](SliceList&, Timeline& t) { edit(t); });
        refreshViews(true);   // the view shows the edit at once, not at the next tick
    };

    startTimerHz(15);
}

// One marker edit: copy the layer's slices and timeline, apply the edit, publish the result (the audio is shared).
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

// Shows the layer's loop, slice markers and timeline; only reads what changed (the data pointer moves on every edit).
void PhraseContent::refreshViews(bool force)
{
    const auto d = host.getPhrase(currentLayer).getData();
    const double tempo = (double) apvts.getRawParameterValue(layerParamPrefix(currentLayer) + ppi::suffix[ppi::ltempo])->load();
    timelineView.setGridBeats(gridBeats((int) apvts.getRawParameterValue(layerParamPrefix(currentLayer) + ppi::suffix[ppi::grid])->load()));
    if (! force && d.get() == shownData && tempo == shownTempo) return;
    shownData = d.get();
    shownTempo = tempo;
    if (d == nullptr || d->audio == nullptr)
    {
        shownAudio = nullptr;
        waveform.setAudio(nullptr);
        waveform.setMarkers({});
        timelineView.setTimeline(Timeline(), {});
        return;
    }

    // Each slice's own length in beats at the loop's tempo: the block widths the timeline draws.
    std::vector<double> beats;
    for (int i = 0; i < d->slices.size(); ++i)
        beats.push_back((double) d->slices.slice(i).length() / d->fileSampleRate * tempo / 60.0);
    timelineView.setTimeline(d->timeline, std::move(beats));
    if (d->audio.get() != shownAudio) { shownAudio = d->audio.get(); waveform.setAudio(d->audio); }
    std::vector<LoopWaveform::Marker> markers;
    for (int i = 0; i < d->slices.size(); ++i) markers.push_back({ d->slices.marker(i).position, d->slices.marker(i).pinned });
    waveform.setMarkers(std::move(markers));
}

void PhraseContent::addKnob(int row, const char* label, MuLookAndFeel::ColourIds colour)
{
    KnobDef d;
    d.comp = std::make_unique<KnobWithLabel>(label, colour);
    d.row = row; d.name = ppi::suffix[row];
    d.comp->onStatusUpdate = [this](const juce::String& n, const juce::String& v) { if (onStatusUpdate) onStatusUpdate(n, v); };
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

    for (auto& k : knobs)
        k.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, prefix + k.name, k.comp->getSlider());
    modeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(apvts, prefix + ppi::suffix[ppi::mode], modeBox.getComboBox());

    bindModulationArcs();
    shownData = nullptr; shownAudio = nullptr;   // another layer's loop: show it afresh
    waveform.setSelectedRegion(-1);
    timerCallback();
    repaint();
}

// Give every knob that has a row in the modulation target table its live modulation arc, driven by the current layer's
// matrix and the value the audio thread published for that target.
void PhraseContent::bindModulationArcs()
{
    static const float kNaN = std::numeric_limits<float>::quiet_NaN();
    const auto* matrix = &host.getPhrase(currentLayer).modulationMatrix;
    const int layer = currentLayer;

    for (auto& k : knobs)
    {
        const int dest = mu_mod::indexOfParam(kModDestTable, k.name.toStdString());
        if (dest < 0) { k.comp->clearModBinding(); continue; }
        k.comp->bindModulation(kModDestTable[dest].id, matrix, [this, layer, dest]
                               { return host.getModLive(layer, dest); });
        juce::ignoreUnused(kNaN);
    }
}

void PhraseContent::chooseLoop()
{
    chooser = std::make_unique<juce::FileChooser>("Load a loop", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this](const juce::FileChooser& fc)
                         {
                             const auto f = fc.getResult();
                             if (f.existsAsFile()) host.loadLoopAsync(currentLayer, f);
                         });
}

void PhraseContent::timerCallback()
{
    const auto text = host.getLoopStatus(currentLayer);
    if (status.getText() != text) status.setText(text, juce::dontSendNotification);
    refreshViews();
}

void PhraseContent::paint(juce::Graphics& g)
{
    MuLookAndFeel::drawAccentPanel(g, getLocalBounds().reduced(2).toFloat(), MuLookAndFeel::appAccent(*this));
}

void PhraseContent::resized()
{
    using mu_ui::s;
    auto area = getLocalBounds().reduced(s(LF::kPanelPad) * 2);

    // Top row: the load button and the status line.
    auto top = area.removeFromTop(s(kStatusH));
    loadButton.setBounds(top.removeFromLeft(s(112)));
    top.removeFromLeft(s(LF::kSpaceS));
    status.setBounds(top);
    area.removeFromTop(s(LF::kSpaceS));

    // Second row: the tempo mode, then the knobs in Size-2 cells.
    auto row = area.removeFromTop(s(LF::kKnobSize2H));
    modeBox.setBounds(row.removeFromLeft(s(108)).withSizeKeepingCentre(s(108), s(kStatusH)));
    row.removeFromLeft(s(LF::kSpaceS));
    for (auto& k : knobs)
        k.comp->setBounds(row.removeFromLeft(s(LF::kKnobSize2W)));

    // The source waveform takes the upper part of the rest of the band, the timeline the lower (the slice player panel is next).
    area.removeFromTop(s(LF::kSpaceS));
    const int timelineH = juce::jmax(s(70), area.getHeight() * 3 / 10);
    timelineView.setBounds(area.removeFromBottom(timelineH));
    area.removeFromBottom(s(LF::kSpaceS));
    waveform.setBounds(area);
}

} // namespace mu_tilate
