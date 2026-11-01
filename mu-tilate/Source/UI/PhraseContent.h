#pragma once

#include "Modulation/MuTilateModDest.h"
#include "Persistence/PhraseParams.h"
#include "UI/Components/DropdownSelect.h"
#include "UI/Components/KnobWithLabel.h"
#include "UI/Components/LoopWaveform.h"
#include "UI/Components/MuLookAndFeel.h"
#include "UI/Components/SegmentControl.h"
#include "UI/LayerContent.h"   // mu-core: the base every layer type's content derives from
#include "Sequencer/SliceList.h"
#include "Sequencer/Timeline.h"
#include "UI/PhraseHost.h"     // what the content needs from the layer's host
#include "UI/PhraseTimeline.h"
#include "UI/SlicePanel.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace mu_tilate
{

// mu-Tilate's content band inside the shared LayerPanel. First version: the loop (load button and a status line) and
// the playback controls (tempo mode, loop tempo, transient hold, strength, fades, level). The source waveform, the
// slices, the slice player and the timeline (design-ui.md) are the next stage.
class PhraseContent : public LayerContent,
                      private juce::Timer
{
public:
    explicit PhraseContent(PhraseHost& host);
    ~PhraseContent() override { stopTimer(); }

    void setLayer(int idx) override;
    void releaseModulationArcs() override { for (auto& k : knobs) k.comp->clearModBinding(); }
    const ModDestProvider* getModDestProvider() const noexcept override { return nullptr; }

    int getLayer() const noexcept { return currentLayer; }

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
    void updateView();                            // shows the waveform or the slice player, whichever the switch says
    void refreshSlicePanel(bool force);           // shows the selected slice's settings in the slice player
    void selectSlice(int slice);                  // selects a slice (-1 for none) in the waveform and the slice player
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
    SegmentControl viewSwitch { { "Waveform", "Slice player" }, SegmentControl::ActiveStyle::General, SegmentControl::DrawStyle::Lcd };
    LoopWaveform waveform;
    SlicePanel slicePanel;
    PhraseTimeline timelineView;
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
