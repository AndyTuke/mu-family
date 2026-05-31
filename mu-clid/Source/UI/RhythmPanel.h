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
#include "Plugin/PluginProcessor.h"

//==============================================================================
// Lightweight modal card for saving a rhythm preset: name + category + embed-samples toggle.
class RhythmSaveDialog : public juce::Component
{
public:
    std::function<void(const juce::String& name,
                       const juce::String& desc,
                       const juce::String& category,
                       bool embed)> onSave;
    std::function<void()> onCancel;

    void setDefaultName(const juce::String& n) { nameEditor.setText(n, false); }
    void setDefaultDescription(const juce::String& d) { pendingDefaultDesc = d; }
    void setDefaultCategory(const juce::String& cat) { pendingDefaultCategory = cat; }
    void setDefaultEmbed(bool embed) { pendingDefaultEmbed = embed; }
    void setKnownCategories(const juce::StringArray& cats);
    juce::String resolveCategory() const;
    bool isSaveAsDefault() const { return saveAsDefaultToggle.getToggleState(); }

    RhythmSaveDialog();
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void visibilityChanged() override;

private:
    juce::TextEditor   nameEditor;
    juce::TextEditor   descEditor;
    DropdownSelect     categoryDropdown;
    juce::TextEditor   newCategoryEditor;
    juce::ToggleButton embedToggle        { "Embed sample in file" };
    juce::ToggleButton saveAsDefaultToggle { "Save as Default" };
    juce::TextButton   saveBtn   { "Save" };
    juce::TextButton   cancelBtn { "Cancel" };
    juce::StringArray  knownCategories;
    juce::String       pendingDefaultCategory;
    juce::String       pendingDefaultDesc;
    bool               pendingDefaultEmbed = false;

    void updateDefaultModeState();

    static constexpr int kCardW = 320;
    static constexpr int kCardH = 240;
};

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
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    void setPresetDropLeft(int) noexcept {}   // no-op: the shared header bar self-lays-out

    // Forwarder: PluginEditor calls this on mixer-effect-algorithm change so
    // the voice-section Amp "Effect" send knob label tracks the mixer.
    void setVoiceEffectSendLabel(const juce::String& name) { voiceSection.setEffectSendLabel(name); }

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

    // Shared per-layer header bar (name / reset / delete / preset / save).
    // `rhythmPresetDropdown` aliases the bar's dropdown so the existing
    // preset-population + selection code is unchanged.
    ChannelHeaderBar headerBar;
    DropdownSelect&  rhythmPresetDropdown = headerBar.getPresetDropdown();
    juce::File lastBrowseDir;
    RhythmSaveDialog    rhythmSaveDialog;

    std::vector<juce::File> rhythmPresetFiles;
    juce::File              loadedRhythmPresetFile;
    juce::StringArray       knownRhythmCategories;

    // Fixed chrome heights/widths
    static constexpr int kHeaderH      = 28;
    static constexpr int kSampleBarH   = 22;
    static constexpr int kVoiceH       = 144;
    static constexpr int kPanelPad     = 6;
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
