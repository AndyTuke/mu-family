#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "RhythmCircle.h"
#include "EuclideanPanel.h"
#include "VoiceSection.h"
#include "UI/ChannelHeaderBar.h"
#include "UI/ModulatorPanel.h"
#include "Modulation/MuClidModDest.h"
#include "UI/Components/DropdownSelect.h"
#include "UI/Components/MuLookAndFeel.h"
#include "UI/SaveDialog.h"
#include "Plugin/PluginProcessor.h"

namespace mu_clid {

// Full rhythm editor panel. Layout (top to bottom):
//   Header bar | Sample bar | [RhythmCircle | EuclideanPanel] | VoiceSection | ModulatorPanel
class RhythmPanel : public juce::Component,
                    public juce::FileDragAndDropTarget,
                    public juce::AudioProcessorValueTreeState::Listener,
                    private juce::Timer
{
public:
    explicit RhythmPanel(PluginProcessor& p);
    ~RhythmPanel() override;

    void setRhythm(int index);
    int  getCurrentRhythmIndex() const noexcept { return currentRhythmIndex; }

    // Propagates merged category list from PluginEditor to the save dialog + browser.
    void setKnownCategories(const juce::StringArray& cats);
    juce::StringArray getKnownCategories() const { return knownRhythmCategories; }

    std::function<void(const juce::String& name,
                       const juce::String& value,
                       juce::Colour rhythmColour)> onStatusUpdate;

    std::function<void()>    onRhythmRenamed;
    std::function<void(int)> onRhythmDeleted;

    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    juce::Rectangle<int> headerRect() const;          // the rhythm header strip
    juce::Rectangle<int> sampleDisplayRect() const;   // the sample LCD inside its strip
    void lookAndFeelChanged() override { resized(); repaint(); }   // screws style moves some content in
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    void setPresetDropLeft(int) noexcept {}   // no-op: the shared header bar self-lays-out

    // PluginEditor calls this on mixer-effect-algorithm change so the voice-section
    // Amp "Effect" send knob and the effect-send modulation target track the mixer.
    void setVoiceEffectSendLabel(const juce::String& name);

private:
    PluginProcessor& proc;
    int currentRhythmIndex = -1;

    // Tracks the most recent euclid overrides applied to the RhythmCircle so the
    // timer can detect modulation-driven changes and refresh the circle without
    // recomputing step types on every tick.
    EuclidOverrides lastCircleOverrides {};

    RhythmCircle    circle;
    EuclideanPanel  euclidPanel;
    VoiceSection    voiceSection;
    ModulatorPanel  modulatorPanel;
    // mu-clid-specific destination provider — wraps the mu-clid kTable +
    // populate logic and is passed into modulatorPanel so the mu-core panel
    // stays product-agnostic.
    ModDestProvider modDestProvider;
    juce::String    effectSendName;   // mixer effect bus's current effect — names the effect-send target

    // Shared per-layer header bar (name / reset / delete / preset / save).
    // `rhythmPresetDropdown` aliases the bar's dropdown so the existing
    // preset-population + selection code is unchanged.
    ChannelHeaderBar headerBar;
    DropdownSelect&  rhythmPresetDropdown = headerBar.getPresetDropdown();
    juce::File lastBrowseDir;
    SaveDialog          saveDialog;   // mu-core: the shared save card

    std::vector<juce::File> rhythmPresetFiles;
    juce::File              loadedRhythmPresetFile;
    juce::StringArray       knownRhythmCategories;

    // Fixed chrome heights/widths
    static constexpr int kHeaderH      = 32;   // ChannelHeaderBar (28) + its rhythm-colour panel outline
    static constexpr int kHeaderInsetY = 2;    // bar sits this far inside the outline, top and bottom
    static constexpr int kHeaderInsetX = 4;
    static constexpr int kSampleBarH   = kHeaderH;   // the same strip as the header, so the sample LCD reads like the preset display
    static constexpr int kVoiceH       = 144;
    static constexpr int kPanelPad     = MuLookAndFeel::kPanelPad;
    static constexpr int kModeSelectorW = 80;
    static constexpr int kIconBtnW     = 22;
    static constexpr int kPresetBtnW   = 38;

    // Computed in resized(), used in both resized() and paint()
    int circleW = 300;
    int topH    = 300;
    juce::Rectangle<int> sampleRect, circleRect, euclidRect, voiceRect, modRect;

    void loadSample();
    void refreshRhythmPresets();
    void saveRhythmPreset();
    void refreshCircle();
    juce::Colour currentColour() const;
    void commitNameFromLabel(const juce::String& rawName);
    void confirmReset();
    void confirmDelete();
    void timerCallback() override;

    // juce::AudioProcessorValueTreeState::Listener — syncs knobs + circle on DAW automation
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void registerRhythmListeners(int ri);
    void deregisterRhythmListeners(int ri);
};

} // namespace mu_clid
