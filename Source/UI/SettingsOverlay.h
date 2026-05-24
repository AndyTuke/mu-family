#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Components/MuClidLookAndFeel.h"
#include "Components/NudgeInput.h"
#include "Components/KnobWithLabel.h"
#include "Components/DropdownSelect.h"
#include "Components/SegmentControl.h"

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

    // UI Size picker (Medium / Large). Writes to PluginProcessor::setUiScale,
    // which persists via appSettings and triggers the editor's onUiScaleChanged
    // callback. The window resizes immediately and layout reflows; ctor-time
    // fonts (#574) only update on next editor open — hint label tells the user.
    juce::Label     uiSizeLabel;
    SegmentControl  uiSizeCtrl { { "Medium", "Large" } };

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

    // primary sample library — user's personal sample folder, opened
    // by default in the sample-load dialog. Distinct from the content folder
    // above which hosts factory + preset-linked material.
    juce::Label      sampleLibLabel;
    juce::TextButton browseSampleLibBtn { "Browse..." };
    juce::TextButton resetSampleLibBtn  { "Default" };

    void updateFolderLabel();
    void updateSampleLibLabel();
    void updateMidiSyncVisibility();

    // Layout sources of truth — populated by computeLayout(), consumed by resized() AND paint()
    // so the section headers can never drift away from the controls they label (the prior
    // implementation mirrored Y values by hand in two places, easy to break on edits).
    struct LayoutY {
        int contentX = 0, contentW = 0;          // centered content column
        int audioHeader = 0, masterVolY = 0;
        int displayHeader = 0, uiSizeRowY = 0;
        int swapHeader = 0, swapRowY = 0;
        int midiClockHeader = 0, clockSourceRowY = 0, midiMessagesRowY = 0;
        int midiPCHeader = 0, midiPresetsRowY = 0;
        int outputHeader = 0, multiBusRowY = 0;
        int sampleLibHeader = 0, sampleLibPathRowY = 0, sampleLibBtnsRowY = 0;
        int contentHeader = 0, contentPathRowY = 0, contentBtnsRowY = 0;
    };
    LayoutY layout;
    void computeLayout();

    static constexpr int kHeaderH       = 44;   // top "Settings" bar
    static constexpr int kPad           = 20;   // page padding
    static constexpr int kSectionGap    = 22;   // vertical gap between sections
    static constexpr int kSectionHeadH  = 28;   // section header band height
    static constexpr int kRowH          = 26;
    static constexpr int kRowGap        = 8;
    static constexpr int kLabelW        = 140;
    static constexpr int kControlW      = 220;
    static constexpr int kContentMaxW   = 620;  // cap content column so it doesn't sprawl
    static constexpr int kLabelCtrlGap  = 12;   // horizontal gap between right-aligned label and control
    // Master vol knob renders at Size 2 (matches voice subsection knobs).
    static constexpr int kMasterVolW    = MuClidLookAndFeel::kKnobSize2W;
    static constexpr int kMasterVolH    = MuClidLookAndFeel::kKnobSize2H;
    static constexpr int kCloseBtnW     = 70;
    static constexpr int kCloseBtnH     = 26;
    static constexpr int kFolderBtnW    = 90;
    static constexpr int kFolderBtnGap  = 8;
};
