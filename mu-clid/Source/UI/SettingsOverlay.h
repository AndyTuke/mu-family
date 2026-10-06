#pragma once

#include "UI/StandardSettingsOverlay.h"   // mu-core: the family-standard settings page

class PluginProcessor;

// mu-Clid's settings page: the family standard (master volume, UI size, standalone MIDI
// Clock — no Transport section, mu-Clid's tempo lives in the transport bar) plus mu-Clid's
// own sections: Output (multi-bus) in General; Hot-swap before MIDI Clock; MIDI Mode
// (plugin only) and the program-change tables after it; and a Locations group (sample
// library + content folder).
class SettingsOverlay : public mu_ui::StandardSettingsOverlay
{
public:
    std::function<void()> onContentDirChanged;

    explicit SettingsOverlay(PluginProcessor& proc);

private:
    PluginProcessor& product;

    // Output — multi-bus toggle (DAW; takes effect after a host rescan).
    juce::ToggleButton multiBusToggle { "Multi-bus output" };

    // Hot-swap timing.
    juce::Label    swapModeLabel;
    DropdownSelect swapModeDropdown;

    // MIDI Note mode (plugin only): Free = host transport, Note = Note On/Off gated.
    juce::Label    midiModeLabel;
    DropdownSelect midiModeDropdown;

    // Locations — the primary sample library (opened by default in the sample-load dialog)
    // and the content folder (factory + preset-linked material).
    juce::Label      sampleLibLabel, contentFolderLabel;
    juce::TextButton browseSampleLibBtn { "Browse..." },    resetSampleLibBtn { "Default" };
    juce::TextButton browseContentFolderBtn { "Browse..." }, resetContentFolderBtn { "Default" };
    std::unique_ptr<juce::FileChooser> fileChooser;
    void updateFolderLabel();
    void updateSampleLibLabel();

    // A folder section: the path on one row, Browse / Default buttons right-aligned below.
    void placeFolderRows(const Rows& r, juce::Label& path, juce::TextButton& browse, juce::TextButton& reset);

    static constexpr int kFolderBtnW   = 90;
    static constexpr int kFolderBtnGap = MuLookAndFeel::kSpaceM;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsOverlay)
};
