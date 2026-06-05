#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "UI/Components/KnobWithLabel.h"
#include "UI/Components/SegmentControl.h"
#include "UI/Components/NudgeInput.h"
#include "UI/Components/MuLookAndFeel.h"

namespace mu_tant
{

class PluginProcessor;

// Basic mu-tant settings page. Appears in place of the main area (the shell's
// gear button toggles it). First cut: master volume + UI size + transport BPM.
// More settings (content folder, MIDI, etc.) land alongside the features that
// need them, mirroring mu-clid's SettingsOverlay structure as it grows.
class SettingsOverlay : public juce::Component
{
public:
    explicit SettingsOverlay(PluginProcessor& proc);

    std::function<void()> onClose;
    // Fired when the user opens a MIDI program-change table; the editor swaps in
    // the shared mu-core overlay (showMidiPresets / showMidiFullPresets).
    std::function<void()> onMidiPresetsClicked;   // Ch 1-8 → per-voice presets
    std::function<void()> onFullPresetsClicked;   // Ch 9   → full presets

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    PluginProcessor& proc;

    juce::TextButton closeBtn { "Close" };

    juce::Label    masterVolLabel;
    KnobWithLabel  masterVolKnob { "Master Vol", MuLookAndFeel::knobLevel };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterVolAttachment;

    juce::Label    uiSizeLabel;
    SegmentControl uiSizeCtrl { { "Medium", "Large" } };

    juce::Label    bpmLabel;
    NudgeInput     bpmInput { "BPM", 20, 300, 120 };

    // ── MIDI Program Change (same model as mu-clid) ──────────────────────────
    // Two tables: per-voice presets on Ch 1-8, full presets on Ch 9. The tables
    // themselves are the shared mu-core overlays; these just open them.
    juce::Label      midiPCLabel;
    juce::TextButton midiPresetsBtn { "Voice Presets" };
    juce::TextButton fullPresetsBtn { "Full Presets" };

    static constexpr int kHeaderH  = 44;
    static constexpr int kRowH     = 28;
    static constexpr int kRowGap   = 18;
    static constexpr int kLabelW   = 140;
    static constexpr int kCtrlW    = 200;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsOverlay)
};

} // namespace mu_tant
