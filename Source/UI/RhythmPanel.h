#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "RhythmCircle.h"
#include "EuclideanPanel.h"
#include "VoiceSection.h"
#include "ModulatorPanel.h"
#include "Components/DropdownSelect.h"
#include "Components/MuClidLookAndFeel.h"
#include "../PluginProcessor.h"

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
    void setPresetDropLeft(int x) noexcept { rhythmDropLeft = x; }

private:
    PluginProcessor& proc;
    int currentRhythmIndex = -1;

    // #407: edge-trigger so the play→stop transition still gets ONE final indicator
    // refresh to clear the cyan mod rings + live arcs from the knobs. Without this,
    // stopping the sequencer would freeze the rings in their last-played state.
    bool wasPlayingLastTick = false;

    RhythmCircle    circle;
    EuclideanPanel  euclidPanel;
    VoiceSection    voiceSection;
    ModulatorPanel  modulatorPanel;

    juce::Label      nameLabel;
    juce::TextButton resetBtn        { juce::String::charToString(0x21BA) }; // ↺
    juce::TextButton deleteBtn       { juce::String::charToString(0x2715) }; // ✕
    DropdownSelect   rhythmPresetDropdown;
    juce::TextButton saveRhythmBtn   { "Save" };
    juce::File lastBrowseDir;
    RhythmSaveDialog    rhythmSaveDialog;

    std::vector<juce::File> rhythmPresetFiles;
    juce::File              loadedRhythmPresetFile;
    juce::StringArray       knownRhythmCategories;
    int                     rhythmDropLeft = 0;  // set by PluginEditor after transport bar layout

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
    juce::Rectangle<int> nameRect;   // header name hit-area

    void loadSample();
    void refreshRhythmPresets();
    void saveRhythmPreset();
    void refreshCircle();
    juce::Colour currentColour() const;
    void commitNameFromLabel();
    void confirmReset();
    void confirmDelete();
    void timerCallback() override;

    // juce::AudioProcessorValueTreeState::Listener — syncs knobs + circle on DAW automation
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void registerRhythmListeners(int ri);
    void deregisterRhythmListeners(int ri);
};
