#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Plugin/ProcessorBase.h"
#include "UI/Components/NudgeInput.h"
#include "UI/Components/DropdownSelect.h"
#include "UI/Components/MuLookAndFeel.h"

// Shared mu-family transport bar — play / BPM / position / preset dropdown /
// new / save / gear / mixer toggle. Plugin-agnostic: takes ProcessorBase, reads
// transport + preset + license through its virtual surface (Phase 2).
//
// The bar reserves a central "loop section" slot between the transport pane
// and the preset dropdown that a product can fill with its own component
// (mu-clid's MasterLoopSection sits here). Lite-style builds without preset /
// mixer chrome opt out via setShowPresetControls(false) + setShowMixerToggle(false).
class TransportBar : public juce::Component,
                     private juce::Timer
{
public:
    explicit TransportBar(ProcessorBase& proc);
    ~TransportBar() override;

    std::function<void()>                      onMixerToggle;
    std::function<void()>                      onLogoClicked;
    std::function<void(const juce::File&)>     onPresetSelected;
    std::function<void()>                      onSavePreset;
    std::function<void()>                      onNewPreset;
    std::function<void()>                      onSettingsToggle;
    // status-bar coverage for the chrome (BPM input). Editor wires this to the
    // global StatusBar; no per-channel colour tag (those come from the panels).
    std::function<void(const juce::String& name, const juce::String& value)> onStatusUpdate;

    // ─── Product-supplied chrome ────────────────────────────────────────────
    void setLogoText(const juce::String& text);
    // When false, hides the preset dropdown + new/save buttons + staging badge.
    // Default true.
    void setShowPresetControls(bool show);
    // When false, hides the mixer-toggle button. Default true.
    void setShowMixerToggle(bool show);
    // When false, hides the gear/settings button. Default true.
    void setShowSettingsButton(bool show);
    // Embed a product-specific component (e.g. mu-clid's master-loop section)
    // between the transport pane and the preset dropdown. Pass `width = 0` or
    // `component = nullptr` to clear. The bar parents the component and lays
    // it out in `loopPaneBounds`.
    void setLoopSection(juce::Component* component, int width);

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
    ProcessorBase& proc;
    const bool isStandalone;

    juce::String     logoText;
    bool             showPresetControls  = true;
    bool             showMixerToggle     = true;
    bool             showSettingsButton  = true;
    juce::Component* loopSection        = nullptr;
    int              loopSectionWidth   = 0;

    juce::TextButton playBtn;
    NudgeInput       bpmInput { "BPM", 20, 300, 120 };

    juce::Label      posLabel;
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
};
