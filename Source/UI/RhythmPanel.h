#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include "RhythmCircle.h"
#include "EuclideanPanel.h"
#include "VoiceSection.h"
#include "ModulatorPanel.h"
#include "Components/MuClidLookAndFeel.h"
#include "../PluginProcessor.h"

// Full rhythm editor panel. Layout (top to bottom):
//   Header bar | Sample bar | [RhythmCircle | EuclideanPanel] | VoiceSection | ModulatorPanel
class RhythmPanel : public juce::Component,
                    public juce::FileDragAndDropTarget
{
public:
    explicit RhythmPanel(PluginProcessor& p);

    void setRhythm(int index);

    std::function<void(const juce::String& name,
                       const juce::String& value,
                       juce::Colour rhythmColour)> onStatusUpdate;

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

    std::unique_ptr<juce::FileChooser> fileChooser;
    std::map<int, juce::String> loadedSampleNames;
    juce::File lastBrowseDir;

    // Fixed chrome heights
    static constexpr int kHeaderH    = 28;
    static constexpr int kSampleBarH = 22;
    static constexpr int kVoiceH     = 80;
    static constexpr int kPanelPad   = 6;

    // Computed in resized(), used in both resized() and paint()
    int circleW = 300;
    int topH    = 300;
    juce::Rectangle<int> sampleRect, circleRect, euclidRect, voiceRect, modRect;

    void loadSample();
    void refreshCircle();
    juce::Colour currentColour() const;
};
