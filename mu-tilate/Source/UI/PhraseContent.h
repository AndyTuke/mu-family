#pragma once

#include "Modulation/MuTilateModDest.h"
#include "Persistence/PhraseParams.h"
#include "UI/Components/DropdownSelect.h"
#include "UI/Components/KnobWithLabel.h"
#include "UI/Components/LoopWaveform.h"
#include "UI/Components/MuLookAndFeel.h"
#include "UI/Components/SegmentControl.h"
#include "UI/Voice/InsertSubsection.h"   // mu-core: the shared per-layer insert panel
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

// mu-Tilate's content band inside the shared LayerPanel: the loop (load button and a status line) and two views of the same
// parts. Sequencer: the waveform to drag slices from, the timeline toolbar (length, grid, delete, clear, zoom) and the timeline.
// Sound Editor: the transient detector, the waveform, a slim timeline to pick a hit on, and the slice panels. Layer: the layer's sound,
// filter and insert effect, each in its own panel. The panel edits the selected hit (every hit on the timeline has its own settings); with no
// hit selected it edits the selected slice, whose settings are what a new hit of that slice starts with.
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

    // A knob on parameter `row`: its short label, the full name and what it does (the status bar's first part), and the
    // unit its value is read in (the status bar's second part).
    void addKnob(int row, const char* label, MuLookAndFeel::ColourIds colour, const juce::String& statusName, mu_fmt::Unit unit);
    void addKnob(int row, const char* label, MuLookAndFeel::ColourIds colour, const juce::String& statusName,
                 std::function<juce::String(double)> format);
    void refreshViews(bool force = false);        // re-reads the layer's loop data, slices and timeline when they changed
    void editSlices(const std::function<void(SliceList&, Timeline&)>& edit);   // one marker edit, published to the layer
    void chooseLoop();
    void updateView();                            // shows the Sequencer or the Sound Editor, whichever the switch says
    void refreshSlicePanel(bool force);           // shows the selected hit's (else the selected slice's) settings in the slice player
    void selectRegion(int slice);                 // selects a slice (-1 for none) in the waveform; no hit is selected then
    void placeSliceAt(int slice, double beat);    // a hit of `slice` at `beat`, starting with the slice's settings, and selects it
    void applyLength(const juce::String& text);   // the length box: parses it and sets the timeline length
    void updateToolbar();                         // enables Delete for a selected hit, shows the current length
    bool soundEditorShown() const noexcept { return viewSwitch.getSelectedIndex() == 1; }
    bool layerViewShown() const noexcept { return viewSwitch.getSelectedIndex() == 2; }
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
    SegmentControl viewSwitch { { "Sequencer", "Sound Editor", "Layer" }, SegmentControl::ActiveStyle::General, SegmentControl::DrawStyle::Lcd };
    LoopWaveform waveform;
    SlicePanel slicePanel;
    PhraseTimeline timelineView;

    // The Sequencer's toolbar over the timeline.
    juce::Label      lengthCaption { {}, "Length" };
    juce::Label      lengthBox;                   // editable: "2 bars", "6 beats", "3/4"
    InsertSubsection insertSub;                   // the layer's insert effect (p{N}_drvChar, p{N}_insP1..4)
    DropdownSelect   filterTypeBox;               // the layer's filter type
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> filterTypeAtt;
    DropdownSelect   gridBox;                     // the snap grid
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> gridAtt;
    juce::TextButton deleteButton { "Delete hit" }, clearButton { "Clear" };
    juce::TextButton waveFitButton { "Fit sample" }, tlOutButton { "-" }, tlInButton { "+" }, tlFitButton { "Fit timeline" };
    std::vector<LF_Section> sections;             // the Sound Editor's two panels, from resized() for paint()
    bool shownPanelIsHit = false;                 // whether the slice player last showed a hit (else a slice)
    double shownLengthBeats = -1.0;
    double shownTempo = -1.0;                     // the loop tempo the timeline's block lengths were computed for
    // What the views last showed, by identity number (a freed PhraseData's address can be reused, a number never is).
    uint64_t shownVersion = ~(uint64_t) 0;        // the PhraseData the waveform and timeline last showed
    uint64_t shownAudioId = 0;                    // the audio the waveform last drew
    std::atomic<float>* tempoParam = nullptr;     // the current layer's Loop Tempo and Grid, for the 15 Hz refresh
    std::atomic<float>* gridParam  = nullptr;
    uint64_t shownPanelVersion = ~(uint64_t) 0;   // the PhraseData the slice player last showed

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhraseContent)
};

} // namespace mu_tilate
