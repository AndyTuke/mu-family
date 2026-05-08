#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "RhythmCircle.h"
#include "EuclideanPanel.h"
#include "VoiceSection.h"
#include "ModulatorPanel.h"
#include "Components/DropdownSelect.h"
#include "Components/MuClidLookAndFeel.h"
#include "../PluginProcessor.h"

// Lightweight modal card for saving a rhythm preset: name + embed-samples toggle.
class RhythmSaveDialog : public juce::Component
{
public:
    std::function<void(const juce::String& name, bool embed)> onSave;
    std::function<void()> onCancel;

    void setDefaultName(const juce::String& n) { nameEditor.setText(n, false); }

    // Called by the owner when the target file already exists; shows a warning and arms
    // the overwrite flag so the next Save press proceeds unconditionally (#148).
    void markFileExists();
    bool isPendingOverwrite() const noexcept { return pendingOverwrite; }

    RhythmSaveDialog();
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void visibilityChanged() override;

private:
    juce::TextEditor   nameEditor;
    juce::ToggleButton embedToggle { "Embed sample in file" };
    juce::Label        statusLabel;
    juce::TextButton   saveBtn   { "Save" };
    juce::TextButton   cancelBtn { "Cancel" };
    bool               pendingOverwrite = false;

    static constexpr int kCardW = 320;
    static constexpr int kCardH = 180;  // taller to accommodate status label
};

// Full rhythm editor panel. Layout (top to bottom):
//   Header bar | Sample bar | [RhythmCircle | EuclideanPanel] | VoiceSection | ModulatorPanel
class RhythmPanel : public juce::Component,
                    public juce::FileDragAndDropTarget,
                    private juce::Timer
{
public:
    explicit RhythmPanel(PluginProcessor& p);

    void setRhythm(int index);

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

private:
    PluginProcessor& proc;
    int currentRhythmIndex = -1;

    RhythmCircle    circle;
    EuclideanPanel  euclidPanel;
    VoiceSection    voiceSection;
    ModulatorPanel  modulatorPanel;

    juce::Label      nameLabel;
    juce::TextButton resetBtn     { juce::String::charToString(0x21BA) }; // ↺
    juce::TextButton deleteBtn    { juce::String::charToString(0x2715) }; // ✕
    juce::TextButton loadRhythmBtn { "Load" };
    juce::TextButton saveRhythmBtn { "Save" };

    std::unique_ptr<juce::FileChooser> fileChooser;
    std::unique_ptr<juce::FileChooser> rhythmLoadChooser;
    juce::File lastBrowseDir;
    RhythmSaveDialog rhythmSaveDialog;

    // Fixed chrome heights/widths
    static constexpr int kHeaderH       = 28;
    static constexpr int kSampleBarH    = 22;
    static constexpr int kVoiceH        = 144;
    static constexpr int kPanelPad      = 6;
    static constexpr int kModeSelectorW  = 80;
    static constexpr int kIconBtnW       = 22;
    static constexpr int kPresetBtnW     = 38;

    // Computed in resized(), used in both resized() and paint()
    int circleW = 300;
    int topH    = 300;
    juce::Rectangle<int> sampleRect, circleRect, euclidRect, voiceRect, modRect;
    juce::Rectangle<int> nameRect;   // header name hit-area

    void loadSample();
    void loadRhythmPreset();
    void saveRhythmPreset();
    void refreshCircle();
    juce::Colour currentColour() const;
    void commitNameFromLabel();
    void confirmReset();
    void confirmDelete();
    void timerCallback() override;
};
