#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"
#include "Components/NudgeInput.h"
#include "Components/DropdownSelect.h"
#include "Components/MuClidLookAndFeel.h"

class TransportBar : public juce::Component, private juce::Timer
{
public:
    explicit TransportBar(PluginProcessor& proc);
    ~TransportBar() override;

    std::function<void()>                      onMixerToggle;
    std::function<void()>                      onLogoClicked;
    std::function<void(const juce::File&)>     onPresetSelected;
    std::function<void()>                      onSavePreset;
    std::function<void()>                      onSettingsToggle;

    void refreshPresets();
    void setMixerActive(bool active);
    void setSaveEnabled(bool enabled);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    PluginProcessor& proc;
    const bool isStandalone;

    juce::TextButton playBtn;
    NudgeInput       bpmInput { "BPM", 20, 300, 120 };

    juce::Label      posLabel;
    juce::Label      loopLabel;
    DropdownSelect   loopDropdown;
    juce::Label      loopStepLabel;
    DropdownSelect   presetDropdown;
    juce::TextButton saveBtn  { "Save" };
    juce::TextButton gearBtn;
    juce::TextButton mixerBtn { "Mixer" };

    static constexpr int kLogoW      = 88;
    static constexpr int kPlayW      = 36;   // wider for clarity
    static constexpr int kBpmW       = 72;   // inline "BPM" label + value + arrows
    static constexpr int kPosW       = 56;
    static constexpr int kLoopLabelW = 36;
    static constexpr int kLoopW      = 100;
    static constexpr int kLoopStepW  = 56;
    static constexpr int kPresetW    = 240;  // wider preset dropdown
    static constexpr int kSaveW      = 44;
    static constexpr int kGearW      = 28;
    static constexpr int kMixerW     = 80;
    static constexpr int kGap        = 6;

    // Sub-pane bounds — computed in resized(), used in paint().
    juce::Rectangle<int> transportPaneBounds;
    juce::Rectangle<int> loopPaneBounds;

    std::vector<juce::File> presetFiles;

    void timerCallback() override;
    void refreshPlayBtn();
    void updatePositionLabel();
    void populatePresetDropdown();
};
