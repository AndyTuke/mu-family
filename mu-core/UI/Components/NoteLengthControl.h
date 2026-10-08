#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/Components/DropdownSelect.h"
#include "UI/Components/NudgeInput.h"
#include "Sequencer/ControlSequence.h"   // NoteValue / NoteMod

// A musical length as the family sets it everywhere — a modulator's Loop and Step, the Rumble
// envelope's Length: a small label, a note value (1 … 1/32, triplet, dotted) and a × multiplier.
// The label / dropdown / multiplier sit left to right at the family widths (preferredWidth).
class NoteLengthControl : public juce::Component
{
public:
    explicit NoteLengthControl(const juce::String& label);

    // Show a length (no onChange).
    void setLength(NoteValue nv, NoteMod mod, int multiplier);

    // Fires when the user picks a note value or changes the multiplier.
    std::function<void(NoteValue nv, NoteMod mod, int multiplier)> onChange;

    // Metal style: LCD dropdown and an engraved-legible label colour.
    void setMetalStyle(bool metal);

    // Unscaled widths (wrap in mu_ui::s at use): label, note dropdown (fits "1/32T"), × multiplier.
    static constexpr int kLabelW = 30, kDropdownW = 58, kMultW = 46, kGap = 2;
    static constexpr int kWidth  = kLabelW + kGap + kDropdownW + kGap + kMultW;
    static constexpr int kHeight = 22;   // a modulator header control's height (its 28 px row less the box inset)
    static constexpr int kMaxMultiplier = 16;

    void resized() override;

private:
    void fire();

    juce::Label    label;
    DropdownSelect noteDropdown;
    NudgeInput     multiplier { juce::String::fromUTF8("\xc3\x97"), 1, kMaxMultiplier, 1 };
    NoteValue      noteValue = NoteValue::Quarter;
    NoteMod        noteMod   = NoteMod::None;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoteLengthControl)
};
