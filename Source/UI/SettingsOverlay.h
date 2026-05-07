#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Components/MuClidLookAndFeel.h"
#include "Components/NudgeInput.h"
#include "Components/KnobWithLabel.h"
#include "Components/DropdownSelect.h"

class PluginProcessor;

// Settings panel overlay.  Appears in place of the main area.
// Active settings write to ApplicationProperties; the rest are placeholders.
class SettingsOverlay : public juce::Component
{
public:
    std::function<void()> onClose;
    std::function<void()> onContentDirChanged;
    std::function<void()> onMidiPresetsClicked;

    explicit SettingsOverlay(PluginProcessor& proc);

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    PluginProcessor& proc;
    const bool isStandalone;

    juce::TextButton closeBtn { "Close" };

    // Active: master volume knob (reads/writes from APVTS)
    KnobWithLabel masterVolKnob { "Master Vol", MuClidLookAndFeel::knobLevel };

    // Hot-swap timing
    juce::Label    swapModeLabel;
    DropdownSelect swapModeDropdown;

    // MIDI sync (standalone only)
    juce::Label    clockSourceLabel;
    DropdownSelect clockSourceDropdown;
    juce::Label    midiMessagesLabel;
    DropdownSelect midiMessagesDropdown;

    // MIDI program-change preset assignments (button opens MidiPresetsPanel overlay).
    juce::TextButton midiPresetsBtn { "MIDI Presets..." };

    // Multi-bus output toggle (DAW only). Requires host rescan to take effect.
    juce::ToggleButton multiBusToggle { "Multi-bus output" };

    // Content folder configuration
    juce::Label      contentFolderLabel;
    juce::TextButton browseContentFolderBtn { "Browse..." };
    juce::TextButton resetContentFolderBtn  { "Default" };
    std::unique_ptr<juce::FileChooser> fileChooser;

    void updateFolderLabel();
    void updateMidiSyncVisibility();

    static constexpr int kHeaderH = 36;
    static constexpr int kPad     = 12;
    static constexpr int kRowH    = 40;
};
