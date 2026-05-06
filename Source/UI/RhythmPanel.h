#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include "RhythmCircle.h"
#include "EuclideanPanel.h"
#include "VoiceSection.h"
#include "ModulatorPanel.h"
#include "Components/DropdownSelect.h"
#include "Components/MuClidLookAndFeel.h"
#include "../PluginProcessor.h"

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
    DropdownSelect  midiModeDropdown;

    juce::Label      nameLabel;
    juce::TextButton resetBtn      { juce::String::charToString(0x21BA) }; // ↺
    juce::TextButton deleteBtn     { juce::String::charToString(0x2715) }; // ✕
    juce::TextButton loadPresetBtn { "R.Pst" };

    std::unique_ptr<juce::FileChooser> fileChooser;
    std::unique_ptr<juce::FileChooser> rhythmPresetChooser;
    std::map<int, juce::String> loadedSampleNames;
    juce::File lastBrowseDir;

    // Fixed chrome heights/widths
    static constexpr int kHeaderH       = 28;
    static constexpr int kSampleBarH    = 22;
    static constexpr int kVoiceH        = 144;
    static constexpr int kPanelPad      = 6;
    static constexpr int kModeSelectorW  = 80;
    static constexpr int kIconBtnW       = 22;
    static constexpr int kPresetBtnW     = 40;

    // Computed in resized(), used in both resized() and paint()
    int circleW = 300;
    int topH    = 300;
    juce::Rectangle<int> sampleRect, circleRect, euclidRect, voiceRect, modRect;
    juce::Rectangle<int> nameRect;   // header name hit-area

    void loadSample();
    void loadRhythmPreset();
    void refreshCircle();
    juce::Colour currentColour() const;
    void commitNameFromLabel();
    void confirmReset();
    void confirmDelete();
    void timerCallback() override;
};
