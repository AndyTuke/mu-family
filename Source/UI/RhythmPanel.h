#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "RhythmCircle.h"
#include "EuclideanPanel.h"
#include "VoiceSection.h"
#include "Components/MuClidLookAndFeel.h"
#include "../PluginProcessor.h"

// Full rhythm editor panel. Layout (top to bottom):
//   Header bar (name + colour) | Sample bar | [RhythmCircle | EuclideanPanel] | VoiceSection | ModulatorPanel (stub)
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

    // FileDragAndDropTarget
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    PluginProcessor& proc;
    int currentRhythmIndex = -1;

    RhythmCircle   circle;
    EuclideanPanel euclidPanel;
    VoiceSection   voiceSection;

    std::unique_ptr<juce::FileChooser> fileChooser;

    static constexpr int kHeaderH     = 28;
    static constexpr int kSampleBarH  = 22;
    static constexpr int kCircleW     = 200;
    static constexpr int kTopH        = 220;
    static constexpr int kVoiceH      = 64;

    void loadSample();
    void refreshCircle();
    juce::Colour currentColour() const;
};
