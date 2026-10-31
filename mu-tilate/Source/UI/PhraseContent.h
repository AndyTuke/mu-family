#pragma once

#include "Modulation/MuTilateModDest.h"
#include "Persistence/PhraseParams.h"
#include "UI/Components/DropdownSelect.h"
#include "UI/Components/KnobWithLabel.h"
#include "UI/Components/LoopWaveform.h"
#include "UI/Components/MuLookAndFeel.h"
#include "UI/LayerContent.h"   // mu-core: the base every layer type's content derives from
#include "Sequencer/SliceList.h"
#include "Sequencer/Timeline.h"
#include "UI/PhraseHost.h"     // what the content needs from the layer's host
#include "UI/PhraseTimeline.h"

#include <juce_gui_basics/juce_gui_basics.h>

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

    void addKnob(int row, const char* label, MuLookAndFeel::ColourIds colour);
    void refreshViews(bool force = false);        // re-reads the layer's loop data, slices and timeline when they changed
    void editSlices(const std::function<void(SliceList&, Timeline&)>& edit);   // one marker edit, published to the layer
    void chooseLoop();
    void timerCallback() override;
    void bindModulationArcs();

    PhraseHost& host;
    juce::AudioProcessorValueTreeState& apvts;
    int currentLayer = 0;

    juce::TextButton loadButton { "Load loop..." };
    juce::Label      status;
    DropdownSelect   modeBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAtt;
    std::vector<KnobDef> knobs;
    std::unique_ptr<juce::FileChooser> chooser;
    LoopWaveform waveform;
    PhraseTimeline timelineView;
    double shownTempo = -1.0;                     // the loop tempo the timeline's block lengths were computed for
    const void* shownData = nullptr;              // the PhraseData the waveform last showed (identity only)
    const void* shownAudio = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhraseContent)
};

} // namespace mu_tilate
