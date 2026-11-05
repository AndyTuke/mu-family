#pragma once

#include "Modulation/MuTilateModDest.h"
#include "Persistence/PhraseParams.h"
#include "UI/Components/DropdownSelect.h"
#include "UI/Components/KnobWithLabel.h"
#include "UI/Components/LoopWaveform.h"
#include "UI/Components/MuLookAndFeel.h"
#include "UI/Components/NudgeInput.h"
#include "UI/Components/IconButton.h"
#include "UI/Components/SourceRangeBar.h"
#include "UI/Components/SegmentControl.h"
#include "UI/Voice/InsertSubsection.h"   // mu-core: the shared per-layer insert panel
#include "UI/Voice/VoiceBand.h"          // mu-core: the shared Pitch | Filter | Amp | Effects band
#include "UI/LayerContent.h"   // mu-core: the base every layer type's content derives from
#include "Sequencer/SliceList.h"
#include "Sequencer/Timeline.h"
#include "UI/PhraseHost.h"     // what the content needs from the layer's host
#include "UI/LengthText.h"
#include "UI/PhraseTimeline.h"
#include "UI/SlicePanel.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace mu_tilate { using LF_Section = MuLookAndFeel::Section; }

namespace mu_tilate
{

// mu-Tilate's content band inside the shared LayerPanel: the loop (load button and a status line) and three views of the same
// parts, in the order the work goes. Sequencer: the waveform to drag slices from, the timeline toolbar (length, grid, lay out,
// delete, clear, zoom) and the timeline. Layer: the layer's sound (tempo mode, tempo, hold, strength) and its Pitch | Filter | Amp |
// Effects, every value absolute. Slice: the transient detector, the waveform, a slim timeline to pick a hit on, and the same
// Pitch | Filter | Amp | Effects for the selected hit, every value an offset added to the layer's. Every hit on the timeline has
// its own settings; with no hit selected the panel edits the selected slice, whose settings new hits of it start with.
class PhraseContent : public LayerContent,
                      private juce::Timer
{
public:
    PhraseContent(ProcessorBase& base, PhraseHost& host);
    ~PhraseContent() override { stopTimer(); }

    void setLayer(int idx) override;
    void releaseModulationArcs() override { for (auto& k : knobs) k.comp->clearModBinding(); }
    const ModDestProvider* getModDestProvider() const noexcept override { return nullptr; }

    int getLayer() const noexcept { return currentLayer; }

    // The layer's insert algorithm changed (the modulation panel's insert targets follow it).
    std::function<void(int insertAlgo)> onInsertAlgorithmChanged;

    void lookAndFeelChanged() override { resized(); repaint(); }
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    struct KnobDef { std::unique_ptr<KnobWithLabel> comp; int row; juce::String name;
                     std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att; };

    // The shape of a slice dragged out of the waveform, following the pointer until it is over the timeline.
    struct DragGhost : public juce::Component
    {
        std::vector<float> peaks;
        void paint(juce::Graphics& g) override;
    };

    // A knob on parameter `row`: its short label, the full name and what it does (the status bar's first part), and the
    // unit its value is read in (the status bar's second part).
    void addKnob(int row, const char* label, MuLookAndFeel::ColourIds colour, const juce::String& statusName, mu_fmt::Unit unit);
    void addKnob(int row, const char* label, MuLookAndFeel::ColourIds colour, const juce::String& statusName,
                 std::function<juce::String(double)> format);
    KnobWithLabel& knobOf(int row);               // the knob on parameter `row`
    void refreshViews(bool force = false);        // re-reads the layer's loop data, slices and timeline when they changed
    void editSlices(const std::function<void(SliceList&, Timeline&)>& edit);   // one marker edit, published to the layer
    void chooseLoop();
    void updateView();                            // shows the view the switch says
    void refreshSlicePanel(bool force);           // shows the selected hit's (else the selected slice's) settings in the slice panel
    void selectRegion(int slice);                 // selects a slice (-1 for none) in the waveform; no hit is selected then
    void placeSliceAt(int slice, double beat);    // a hit of `slice` at `beat`, starting with the slice's settings, and selects it
    void applyLength();   // the Bars and Beats toggles: sets the timeline length from them
    void updateToolbar();                         // enables Delete for a selected hit, shows the current length
    void followLayerChoices();                    // tells the slice panel which filter type and insert effect the layer has
    bool layerViewShown() const noexcept { return viewSwitch.getSelectedIndex() == 1; }
    bool sliceViewShown() const noexcept { return viewSwitch.getSelectedIndex() == 2; }
    void timerCallback() override;
    void bindModulationArcs();

    PhraseHost& host;
    juce::AudioProcessorValueTreeState& apvts;
    int currentLayer = 0;

    juce::TextButton loadButton { "Load loop..." };
    juce::Label      status;
    DropdownSelect   modeBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAtt;
    DropdownSelect   detectBox;                   // the detection method
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> detectAtt;
    juce::TextButton detectButton { "Detect" };   // runs the detector again with the current settings
    std::vector<KnobDef> knobs;
    std::unique_ptr<juce::FileChooser> chooser;
    SegmentControl viewSwitch { { "Sequencer", "Layer", "Slice" }, SegmentControl::ActiveStyle::General, SegmentControl::DrawStyle::Lcd };
    LoopWaveform waveform;
    SlicePanel slicePanel;
    PhraseTimeline timelineView;
    DragGhost dragGhost;

    // The Layer view: the sound strip and the voice band.
    VoiceBand        layerBand;
    VoiceBandSection layerPitchSection  { 4, VoiceBand::kCols };
    VoiceBandSection layerFilterSection { 6, VoiceBand::kFilterColW };
    VoiceBandSection layerAmpSection    { 4, VoiceBand::kCols };
    InsertSubsection insertSub;                   // the layer's insert effect (p{N}_drvChar, p{N}_insP1..4)
    DropdownSelect   filterTypeBox;               // the layer's filter type
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> filterTypeAtt;

    // The Sequencer's toolbar over the timeline.
    juce::Label      lengthCaption { {}, "Length" };
    NudgeInput       barsInput  { "Bars", 0, 64, 4 };   // the sequence length: whole bars plus extra beats
    NudgeInput       beatsInput { "Beats", 0, 3, 0 };
    SourceRangeBar   rangeBar;                    // the whole source file with the section used (drag its ends to trim)
    DropdownSelect   gridBox;                     // the snap grid
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> gridAtt;
    juce::TextButton deleteButton { "Delete hit" }, clearButton { "Clear" }, layOutButton { "Lay out" };
    // The audition controls are small icon buttons: play at the tempo (metronome), play the original (waveform), stop, and repeat.
    IconButton previewTempoButton { IconButton::Glyph::Play, IconButton::Badge::Metronome }, previewOriginalButton { IconButton::Glyph::Play, IconButton::Badge::Wave },
               previewStopButton { IconButton::Glyph::Stop }, previewLoopButton { IconButton::Glyph::Loop };
    IconButton followButton { IconButton::Glyph::Walk };   // the timeline pages on with the playhead (on) or stays where it is (off)
    juce::TextButton cropButton { "Crop" };       // cuts the sample to the play area (the handles on the waveform)
    juce::TextButton waveFitButton { "Fit sample" }, waveOutButton { "-" }, waveInButton { "+" }, tlOutButton { "-" }, tlInButton { "+" }, tlFitButton { "Fit timeline" };
    std::vector<LF_Section> sections;             // the panels of the Layer and Slice views, from resized() for paint()
    bool shownPanelIsHit = false;                 // whether the slice panel last showed a hit (else a slice)
    double shownLengthBeats = -1.0;
    double shownTempo = -1.0;                     // the loop tempo the timeline's block lengths were computed for
    // What the views last showed, by identity number (a freed PhraseData's address can be reused, a number never is).
    uint64_t shownVersion = ~(uint64_t) 0;        // the PhraseData the waveform and timeline last showed
    uint64_t shownAudioId = 0;                    // the audio the waveform last drew
    std::atomic<float>* tempoParam = nullptr;     // the current layer's Loop Tempo and Grid, for the 15 Hz refresh
    std::atomic<float>* gridParam  = nullptr;
    std::atomic<float>* filterTypeParam = nullptr;   // the layer's filter type and insert effect, followed by the slice panel
    std::atomic<float>* insertAlgoParam = nullptr;
    int shownFilterType = -1, shownInsertAlgo = -1;
    uint64_t shownPanelVersion = ~(uint64_t) 0;   // the PhraseData the slice panel last showed
    std::vector<std::vector<float>> slicePeaks;   // each slice's waveform columns, as the timeline and the drag preview draw them
    int draggedSlice = -1;                        // the slice being dragged out of the waveform

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhraseContent)
};

} // namespace mu_tilate
