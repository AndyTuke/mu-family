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
    std::function<void()>                      onAddRhythm;
    std::function<void(const juce::File&)>     onPresetSelected;
    std::function<void()>                      onSavePreset;
    std::function<void()>                      onSettingsToggle;

    void refreshPresets();
    void setMixerActive(bool active);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    PluginProcessor& proc;
    const bool isStandalone;

    juce::TextButton playBtn;
    NudgeInput       bpmInput { "BPM", 20, 300, 120 };

    juce::Label      posLabel;
    juce::Label      rhythmCountLabel;
    DropdownSelect   loopDropdown;
    juce::TextButton addRhythmBtn { "+" };
    DropdownSelect   presetDropdown;
    juce::TextButton saveBtn  { "Save" };
    juce::TextButton gearBtn;
    juce::TextButton mixerBtn { "Mixer" };

    static constexpr int kLogoW     = 88;
    static constexpr int kPlayW     = 28;
    static constexpr int kBpmW      = 120;
    static constexpr int kPosW      = 80;
    static constexpr int kRhCountW  = 36;
    static constexpr int kLoopW     = 68;
    static constexpr int kAddW      = 28;
    static constexpr int kPresetW   = 140;
    static constexpr int kSaveW     = 44;
    static constexpr int kGearW     = 28;
    static constexpr int kMixerW    = 80;
    static constexpr int kGap       = 6;

    std::vector<juce::File> presetFiles;

    void timerCallback() override;
    void refreshPlayBtn();
    void updatePositionLabel();
    void updateRhythmCount();
    void populatePresetDropdown();
};
