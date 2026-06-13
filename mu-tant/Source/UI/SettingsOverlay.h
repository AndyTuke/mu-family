#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "UI/SettingsOverlayBase.h"
#include "UI/Components/KnobWithLabel.h"
#include "UI/Components/SegmentControl.h"
#include "UI/Components/NudgeInput.h"
#include "UI/Components/MuLookAndFeel.h"

namespace mu_tant
{

class PluginProcessor;

// mu-tant settings page. Shares the header bar / Close button / centred content
// column / group + section header styling with mu-clid via mu_ui::SettingsOverlayBase,
// so both products' settings pages look identical. Content: master volume + UI size
// + transport BPM + MIDI program-change tables, grouped General / MIDI.
class SettingsOverlay : public mu_ui::SettingsOverlayBase
{
public:
    explicit SettingsOverlay(PluginProcessor& proc);

    // Fired when the user opens a MIDI program-change table; the editor swaps in
    // the shared mu-core overlay (showMidiPresets / showMidiFullPresets).
    std::function<void()> onMidiPresetsClicked;   // Ch 1-8 → per-voice presets
    std::function<void()> onFullPresetsClicked;   // Ch 9   → full presets

    void paintContent(juce::Graphics& g) override;
    void layoutContent() override;

private:
    PluginProcessor& proc;

    // Audio — master volume (reads/writes the mixer master fader param directly,
    // not via a SliderAttachment — the attachment overwrites the dB
    // textFromValueFunction with the parameter's raw formatter, mirrors mu-clid).
    KnobWithLabel  masterVolKnob { "Master Vol", MuLookAndFeel::knobLevel };

    // Display — UI size (Medium / Large).
    juce::Label    uiSizeLabel;
    SegmentControl uiSizeCtrl { { "Medium", "Large" } };

    // Transport — internal free-running BPM.
    juce::Label    bpmLabel;
    NudgeInput     bpmInput { "BPM", 20, 300, 120 };

    // MIDI Program Change — two tables (Ch 1-8 voice presets, Ch 9 full presets).
    // The tables themselves are the shared mu-core overlays; these just open them.
    juce::TextButton midiPresetsBtn { "Voice Presets" };
    juce::TextButton fullPresetsBtn { "Full Presets" };

    // Layout sources of truth — populated by computeLayout(), consumed by
    // layoutContent() AND paintContent() so headers can't drift from their rows.
    struct LayoutY {
        int contentX = 0, contentW = 0;
        int generalGroupHeader = 0;
        int audioHeader = 0, masterVolY = 0;
        int displayHeader = 0, uiSizeRowY = 0;
        int transportHeader = 0, bpmRowY = 0;
        int midiGroupHeader = 0;
        int midiPCHeader = 0, midiPCRowY = 0;
    };
    LayoutY layout;
    void computeLayout();

    // Master vol knob renders at Size 2 (matches mu-clid + voice subsection knobs).
    static constexpr int kMasterVolW = MuLookAndFeel::kKnobSize2W;
    static constexpr int kMasterVolH = MuLookAndFeel::kKnobSize2H;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsOverlay)
};

} // namespace mu_tant
