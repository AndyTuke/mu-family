#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "UI/SettingsOverlayBase.h"
#include "UI/Components/KnobWithLabel.h"
#include "UI/Components/SegmentControl.h"
#include "UI/Components/NudgeInput.h"
#include "UI/Components/DropdownSelect.h"
#include "UI/Components/MuLookAndFeel.h"

namespace mu_on
{

class PluginProcessor;

// mu-on settings page. Shares the header bar / Close button / centred content column /
// group + section styling with mu-clid + mu-tant via mu_ui::SettingsOverlayBase, so every
// product's settings page looks identical. Content: master volume + UI size + transport
// BPM (General) and the standalone-only MIDI Clock rows (MIDI). Note mode + Program Change
// arrive when mu-on's note/preset semantics are settled (see backlog).
class SettingsOverlay : public mu_ui::SettingsOverlayBase
{
public:
    explicit SettingsOverlay(PluginProcessor& proc);

    void paintContent(juce::Graphics& g) override;
    void layoutContent() override;

private:
    PluginProcessor& proc;
    const bool isStandalone;

    // Audio — master volume (reads/writes mstr_lvl directly, not via a SliderAttachment;
    // the attachment overwrites the dB textFromValueFunction — mirrors mu-clid/mu-tant).
    KnobWithLabel  masterVolKnob { "Master Vol", MuLookAndFeel::knobLevel };

    // Display — UI size (Medium / Large).
    juce::Label    uiSizeLabel;
    SegmentControl uiSizeCtrl { { "Medium", "Large" } };

    // Transport — internal free-running BPM.
    juce::Label    bpmLabel;
    NudgeInput     bpmInput { "BPM", 20, 300, 120 };

    // MIDI Clock (standalone only) — slave the beat/tempo to external MIDI clock.
    juce::Label    clockSourceLabel;
    DropdownSelect clockSourceDropdown;
    juce::Label    midiMessagesLabel;
    DropdownSelect midiMessagesDropdown;
    void updateMidiSyncVisibility();

    // Layout sources of truth — populated by computeLayout(), consumed by
    // layoutContent() AND paintContent() so headers can't drift from their rows.
    struct LayoutY {
        int contentX = 0, contentW = 0;
        int generalGroupHeader = 0;
        int audioHeader = 0, masterVolY = 0;
        int displayHeader = 0, uiSizeRowY = 0;
        int transportHeader = 0, bpmRowY = 0;
        int midiGroupHeader = 0;
        int midiClockHeader = 0, clockSourceRowY = 0, midiMessagesRowY = 0;
    };
    LayoutY layout;
    void computeLayout();

    // Master vol knob renders at Size 2 (matches mu-clid + voice subsection knobs).
    static constexpr int kMasterVolW = MuLookAndFeel::kKnobSize2W;
    static constexpr int kMasterVolH = MuLookAndFeel::kKnobSize2H;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsOverlay)
};

} // namespace mu_on
