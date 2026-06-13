#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/SettingsOverlayBase.h"
#include "UI/Components/MuLookAndFeel.h"
#include "UI/Components/NudgeInput.h"
#include "UI/Components/KnobWithLabel.h"
#include "UI/Components/DropdownSelect.h"
#include "UI/Components/SegmentControl.h"

class PluginProcessor;

// Settings panel overlay.  Appears in place of the main area.
// Active settings write to ApplicationProperties; the rest are placeholders.
// The header bar, Close button, centred content column and group/section header
// drawing come from mu_ui::SettingsOverlayBase; this class supplies the rows.
class SettingsOverlay : public mu_ui::SettingsOverlayBase
{
public:
    std::function<void()> onContentDirChanged;
    std::function<void()> onMidiPresetsClicked;
    std::function<void()> onFullPresetsClicked;

    explicit SettingsOverlay(PluginProcessor& proc);

    void layoutContent() override;
    void paintContent(juce::Graphics& g) override;

private:
    PluginProcessor& proc;
    const bool isStandalone;

    // Active: master volume knob (reads/writes from APVTS)
    KnobWithLabel masterVolKnob { "Master Vol", MuLookAndFeel::knobLevel };

    // UI Size picker (Medium / Large). Writes to PluginProcessor::setUiScale,
    // which persists via appSettings and triggers the editor's onUiScaleChanged
    // callback. The window resizes immediately and layout reflows; ctor-time
    // fonts only update on next editor open — hint label tells the user.
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

    // MIDI Note mode (plugin only): Free = host transport, Note = Note On/Off gated.
    juce::Label    midiModeLabel;
    DropdownSelect midiModeDropdown;

    // MIDI program-change preset assignments (button opens MidiPresetsPanel overlay).
    juce::TextButton midiPresetsBtn { "Rhythm Preset Table" };
    // Ch-9 full-preset program-change map (button opens MidiFullPresetsPanel overlay).
    juce::TextButton fullPresetsBtn { "Main Preset Table" };

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

        // General sub-panel (Audio, Display, Output)
        int generalGroupHeader = 0;
        int audioHeader = 0, masterVolY = 0;
        int displayHeader = 0, uiSizeRowY = 0;
        int outputHeader = 0, multiBusRowY = 0;

        // MIDI sub-panel (Hot-swap, MIDI Clock, MIDI Program Change)
        int midiGroupHeader = 0;
        int swapHeader = 0, swapRowY = 0;
        int midiClockHeader = 0, clockSourceRowY = 0, midiMessagesRowY = 0;
        int midiModeHeader = 0, midiModeRowY = 0;   // plugin-only
        int midiPCHeader = 0, midiPCRowY = 0;    // single row, two buttons side-by-side

        // Locations sub-panel (Sample Library, Content Folder)
        int locationsGroupHeader = 0;
        int sampleLibHeader = 0, sampleLibPathRowY = 0, sampleLibBtnsRowY = 0;
        int contentHeader = 0, contentPathRowY = 0, contentBtnsRowY = 0;
    };
    LayoutY layout;
    void computeLayout();

    // Shared chrome constants (kHeaderH, kPad, kRowH, kLabelW, kControlW, …) are
    // inherited from mu_ui::SettingsOverlayBase. Only mu-clid-specific sizes live here.
    // Master vol knob renders at Size 2 (matches voice subsection knobs).
    static constexpr int kMasterVolW    = MuLookAndFeel::kKnobSize2W;
    static constexpr int kMasterVolH    = MuLookAndFeel::kKnobSize2H;
    static constexpr int kFolderBtnW    = 90;
    static constexpr int kFolderBtnGap  = 8;
};
