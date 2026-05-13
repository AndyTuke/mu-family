#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "RhythmCircle.h"
#include "EuclideanPanel.h"
#include "VoiceSection.h"
#include "ModulatorPanel.h"
#include "Components/DropdownSelect.h"
#include "Components/MuClidLookAndFeel.h"
#include "../PluginProcessor.h"

// Inline overlay listing .muRhyth files from the rhythms folder.
// Appears as a modal card over the RhythmPanel area.  Call setAccentColour()
// with the current rhythm colour before making it visible.
class RhythmPresetBrowser : public juce::Component,
                            public juce::ListBoxModel
{
public:
    std::function<void(const juce::File&)> onLoad;
    std::function<void()>                  onClose;

    void setAccentColour(juce::Colour c);
    void refresh(const juce::File& rhythmsDir);

    RhythmPresetBrowser();

    // ListBoxModel
    int  getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool sel) override;
    void listBoxItemClicked(int row, const juce::MouseEvent& e) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    void applyFilter();
    void loadSelected();
    juce::Rectangle<int> cardBounds() const;

    juce::TextEditor searchBox;
    juce::ListBox    listBox;
    juce::TextButton loadBtn   { "Load" };
    juce::TextButton cancelBtn { "Cancel" };

    juce::File              dir;
    std::vector<juce::File> files;
    std::vector<int>        filtered;
    int                     selectedRow = -1;
    juce::Colour            accent { 0xff44cc88 };

    static constexpr int kCardW   = 340;
    static constexpr int kCardH   = 400;
    static constexpr int kHeaderH = 36;
    static constexpr int kSearchH = 32;
    static constexpr int kBotH    = 44;
    static constexpr int kPad     = 8;
};

//==============================================================================
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
                    public juce::AudioProcessorValueTreeState::Listener,
                    private juce::Timer
{
public:
    explicit RhythmPanel(PluginProcessor& p);
    ~RhythmPanel() override;

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
    juce::File lastBrowseDir;
    RhythmPresetBrowser rhythmBrowser;
    RhythmSaveDialog    rhythmSaveDialog;

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

    // juce::AudioProcessorValueTreeState::Listener — syncs knobs + circle on DAW automation
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void registerRhythmListeners(int ri);
    void deregisterRhythmListeners(int ri);
};
