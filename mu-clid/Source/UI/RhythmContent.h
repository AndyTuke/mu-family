#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "EuclidPanelAliases.h"
#include "VoiceSection.h"
#include "MidiOutSection.h"
#include "UI/LayerHeaderBar.h"
#include "UI/ParamRefreshQueue.h"
#include "UI/Components/DropdownSelect.h"
#include "UI/Components/SegmentControl.h"
#include "UI/Components/MuLookAndFeel.h"
#include "UI/SaveDialog.h"
#include "UI/RhythmHost.h"   // what the content reads from the layer's host
#include "UI/LayerContent.h"   // mu-core: the base every layer type's content derives from

class ProcessorBase;

namespace mu_clid {

// mu-Clid's content band inside the shared LayerPanel (kLayerContentH tall), top to bottom:
//   Sample bar (+ Sample / MIDI output selector) | [RhythmCircle | EuclideanPanel]
//   | VoiceSection (or, in MIDI output mode, MidiOutSection)
// The header strip (name, presets, reset / delete) and the modulators belong to the LayerPanel;
// this band wires the rhythm-specific header behaviour (attachHeader).
class RhythmContent : public LayerContent,
                    public juce::FileDragAndDropTarget,
                    public juce::AudioProcessorValueTreeState::Listener,
                    private juce::Timer
{
public:
    // `base` is handed only to the shared Euclid panel and insert section (mu-core components);
    // everything the content itself reads comes through `host`.
    RhythmContent(ProcessorBase& base, RhythmHost& host);
    ~RhythmContent() override;

    // Bind the band to rhythm `index` (the LayerPanel calls this on a layer change).
    void setLayer(int index) override;
    int  getLayer() const noexcept { return currentRhythmIndex; }

    // Lets go of every modulation arc and the insert meter, which point into the shown rhythm's matrix and engine,
    // before that layer is removed, replaced or moved; setLayer rebinds them. For a host that keeps one content
    // for every Rhythm slot (mu-Tronic).
    void releaseModulationArcs() override;

    // Wire the shared header bar's rhythm-specific behaviour (rename, preset list / load / save,
    // reset, delete). The LayerPanel owns the bar; this band only supplies what the buttons do.
    void attachHeader(LayerHeaderBar& bar);

    // Ask the owner to rebind the whole layer (header, modulators, this band) after a reset or load.
    std::function<void(int)> requestRebind;

    // The insert algorithm changed: the editor re-labels the modulators' insert destinations.
    std::function<void(int)> onInsertAlgorithmChanged;

    // The save card, to be hosted over the whole panel (LayerPanel::addOverlay).
    SaveDialog& getSaveDialog() noexcept { return saveDialog; }

    // Propagates merged category list from PluginEditor to the save dialog + browser.
    void setKnownCategories(const juce::StringArray& cats);
    juce::StringArray getKnownCategories() const { return knownRhythmCategories; }

    // A knob hover or change with the shown rhythm's colour (mu-Clid's status bar tints with it). When unset the
    // base's colourless onStatusUpdate gets the same name and value, which is what a slot host listens to.
    std::function<void(const juce::String& name,
                       const juce::String& value,
                       juce::Colour rhythmColour)> onColouredStatus;

    // Fired when the shown rhythm's output mode (Sample / MIDI) changes, so the mixer can show its strip
    // active or inactive.
    std::function<void()>    onOutputModeChanged;

    std::function<void()>    onRhythmRenamed;

    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    juce::Rectangle<int> sampleDisplayRect() const;   // the sample LCD inside its strip (left of the output selector)
    juce::Rectangle<int> outputSelectorRect() const;  // the Sample / MIDI selector at the strip's right end
    bool isMidiOutputMode() const;
    int  outputInsetX() const;                        // the selector's distance from the strip's right edge                    // the shown rhythm is in MIDI output mode
    void lookAndFeelChanged() override { resized(); repaint(); }   // screws style moves some content in
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    void setPresetDropLeft(int) noexcept {}   // no-op: the shared header bar self-lays-out

    // PluginEditor calls this on mixer-effect-algorithm change so the voice-section
    // Amp "Effect" send knob tracks the mixer.
    void setVoiceEffectSendLabel(const juce::String& name);

private:
    RhythmHost& host;
    int currentRhythmIndex = -1;

    // Tracks the most recent euclid overrides applied to the RhythmCircle so the
    // timer can detect modulation-driven changes and refresh the circle without
    // recomputing step types on every tick.
    EuclidOverrides lastCircleOverrides {};

    RhythmCircle    circle;
    EuclideanPanel  euclidPanel;
    VoiceSection    voiceSection;
    MidiOutSection  midiOutSection;
    // Per-rhythm output: Sample plays the loaded sample, MIDI sends notes. Lives in the sample strip.
    SegmentControl  outputCtrl { { "Sample", "MIDI" }, SegmentControl::ActiveStyle::General, SegmentControl::DrawStyle::Lcd };

    // The LayerPanel's header bar (attachHeader); `rhythmPresetDropdown()` is the bar's dropdown.
    LayerHeaderBar* headerBar = nullptr;
    DropdownSelect&   rhythmPresetDropdown() { return headerBar->getPresetDropdown(); }
    juce::File lastBrowseDir;
    SaveDialog          saveDialog;   // mu-core: the shared save card

    std::vector<juce::File> rhythmPresetFiles;
    juce::File              loadedRhythmPresetFile;
    juce::StringArray       knownRhythmCategories;

    // Fixed chrome heights/widths
    static constexpr int kHeaderInsetX = 4;
    static constexpr int kSampleBarH   = MuLookAndFeel::kLayerHeaderStripH;   // the same strip as the header, so the sample LCD reads like the preset display
    static constexpr int kVoiceH       = 144;
    static_assert(kSampleBarH + MuLookAndFeel::kCircleSize + kVoiceH == MuLookAndFeel::kLayerContentH,
                  "sample bar + circle + voice band must tile the layer content band");
    static constexpr int kPanelPad     = MuLookAndFeel::kPanelPad;
    static constexpr int kModeSelectorW = 80;
    static constexpr int kOutputSelectorW = 112;   // the Sample / MIDI selector (two LCD windows) in the sample strip
    static constexpr int kIconBtnW     = 22;
    static constexpr int kPresetBtnW   = 38;

    // Computed in resized(), used in both resized() and paint()
    int circleW = 300;
    int topH    = 300;
    juce::Rectangle<int> sampleRect, circleRect, euclidRect, voiceRect;

    void reportStatus(const juce::String& name, const juce::String& value);
    void loadSample();
    void updateOutputModeUi();   // show the voice band or the MIDI Out section, and the selector, for the shown rhythm
    void refreshRhythmPresets();
    void saveRhythmPreset();
    void refreshCircle();
    juce::Colour currentColour() const;
    void commitNameFromLabel(const juce::String& rawName);
    void confirmReset();
    void timerCallback() override;

    // juce::AudioProcessorValueTreeState::Listener — syncs knobs + circle on DAW automation
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void registerRhythmListeners(int ri);
    void deregisterRhythmListeners(int ri);

    // Refresh the one control a parameter id belongs to (message thread).
    void refreshForParameter(const juce::String& parameterID);

    // Parameter ids that arrive on a non-message thread (host automation on the audio thread) wait here
    // for timerCallback, which refreshes their controls on the message thread.
    mu_ui::ParamRefreshQueue refreshQueue;
};

} // namespace mu_clid
