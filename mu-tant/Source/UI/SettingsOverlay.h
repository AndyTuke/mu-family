#pragma once

#include "UI/StandardSettingsOverlay.h"   // mu-core: the family-standard settings page

namespace mu_tant
{

class PluginProcessor;

// mu-Tant's settings page: the family standard (master volume, UI size, tempo, standalone
// MIDI Clock) plus mu-Tant's MIDI sections — Hot-swap timing before MIDI Clock, then Note
// Mode and the two program-change tables after it.
class SettingsOverlay : public mu_ui::StandardSettingsOverlay
{
public:
    explicit SettingsOverlay(PluginProcessor& proc);

    // Fired when the user opens a MIDI program-change table; the editor swaps in the shared
    // mu-core overlay (showMidiPresets / showMidiFullPresets).
    std::function<void()> onMidiPresetsClicked;   // Ch 1-8 → per-voice presets
    std::function<void()> onFullPresetsClicked;   // Ch 9   → full presets

private:
    PluginProcessor& product;

    // Hot-swap — when a staged preset / program-change swap commits.
    juce::Label    swapModeLabel;
    DropdownSelect swapModeDropdown;

    // Note mode (Free / Note) — shown in standalone and plugin (it works from a keyboard too).
    juce::Label    noteModeLabel;
    DropdownSelect noteModeDropdown;

    // Program change — the tables are the shared mu-core overlays; these just open them.
    juce::TextButton midiPresetsBtn { "Voice Presets" };
    juce::TextButton fullPresetsBtn { "Full Presets" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsOverlay)
};

} // namespace mu_tant
