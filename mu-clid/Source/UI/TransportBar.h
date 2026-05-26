#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Plugin/PluginProcessor.h"
#include "UI/Components/NudgeInput.h"
#include "UI/Components/DropdownSelect.h"
#include "UI/Components/MuClidLookAndFeel.h"

class TransportBar : public juce::Component,
                     public juce::AudioProcessorValueTreeState::Listener,
                     private juce::Timer
{
public:
    explicit TransportBar(PluginProcessor& proc);
    ~TransportBar() override;

    std::function<void()>                      onMixerToggle;
    std::function<void()>                      onLogoClicked;
    std::function<void(const juce::File&)>     onPresetSelected;
    std::function<void()>                      onSavePreset;
    std::function<void()>                      onNewPreset;
    std::function<void()>                      onSettingsToggle;
    // status-bar coverage for chrome controls (bpmInput, loopDropdown).
    // Plugin editor wires this to the global StatusBar; no rhythm-colour tag.
    std::function<void(const juce::String& name, const juce::String& value)> onStatusUpdate;

    void refreshPresets();
    // keep dropdown displaying the loaded preset's name. Pass an invalid
    // File to revert to the "<unnamed preset>" placeholder.
    void setLoadedPreset(const juce::File& file);
    // Returns the currently loaded preset file, or an invalid File if none selected.
    juce::File getLoadedPresetFile() const;
    // Left x of the preset dropdown in TransportBar's own coordinate space
    // (equals editor x since TransportBar is always at x=0).
    int getPresetDropdownLeft() const noexcept;
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
    juce::Label      presetStagingBadge;   // "SWP" pill shown on a pending full-preset hot-swap
    juce::TextButton newBtn   { "New" };
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
    static constexpr int kNewW       = 36;
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

    // Sync `loopDropdown` + `loopStepLabel` from current APVTS state. Called
    // from the ctor and from `parameterChanged("mstrLoop", ...)` so DAW
    // automation of the master-loop length stays mirrored in the UI.
    void syncLoopDropdownFromAPVTS();

    // juce::AudioProcessorValueTreeState::Listener — only subscribed to
    // "mstrLoop". Bounces to the message thread because host automation can
    // fire this on the audio thread.
    void parameterChanged(const juce::String& parameterID, float newValue) override;
};
