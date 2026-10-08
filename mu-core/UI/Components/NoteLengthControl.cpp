#include "UI/Components/NoteLengthControl.h"
#include "UI/Components/MuLookAndFeel.h"

namespace
{
// The note values the dropdown offers (id = index + 1).
struct NoteEntry { NoteValue nv; NoteMod mod; const char* label; };
constexpr NoteEntry kNoteEntries[] = {
    { NoteValue::Whole,        NoteMod::None,    "1"     },
    { NoteValue::Half,         NoteMod::None,    "1/2"   },
    { NoteValue::Quarter,      NoteMod::None,    "1/4"   },
    { NoteValue::Eighth,       NoteMod::None,    "1/8"   },
    { NoteValue::Sixteenth,    NoteMod::None,    "1/16"  },
    { NoteValue::ThirtySecond, NoteMod::None,    "1/32"  },
    { NoteValue::Whole,        NoteMod::Triplet, "1T"    },
    { NoteValue::Half,         NoteMod::Triplet, "1/2T"  },
    { NoteValue::Quarter,      NoteMod::Triplet, "1/4T"  },
    { NoteValue::Eighth,       NoteMod::Triplet, "1/8T"  },
    { NoteValue::Sixteenth,    NoteMod::Triplet, "1/16T" },
    { NoteValue::ThirtySecond, NoteMod::Triplet, "1/32T" },
    { NoteValue::Whole,        NoteMod::Dotted,  "1."    },
    { NoteValue::Half,         NoteMod::Dotted,  "1/2."  },
    { NoteValue::Quarter,      NoteMod::Dotted,  "1/4."  },
    { NoteValue::Eighth,       NoteMod::Dotted,  "1/8."  },
    { NoteValue::Sixteenth,    NoteMod::Dotted,  "1/16." },
    { NoteValue::ThirtySecond, NoteMod::Dotted,  "1/32." },
};
constexpr int kNoteEntryCount = (int) (sizeof(kNoteEntries) / sizeof(kNoteEntries[0]));

int noteToId(NoteValue nv, NoteMod mod)
{
    for (int i = 0; i < kNoteEntryCount; ++i)
        if (kNoteEntries[i].nv == nv && kNoteEntries[i].mod == mod)
            return i + 1;
    return 3;   // 1/4
}
} // namespace

NoteLengthControl::NoteLengthControl(const juce::String& labelText)
{
    label.setText(labelText, juce::dontSendNotification);
    label.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
    label.setJustificationType(juce::Justification::centredRight);
    label.setColour(juce::Label::textColourId, MuLookAndFeel::colour(MuLookAndFeel::mutedText));
    addAndMakeVisible(label);

    for (int i = 0; i < kNoteEntryCount; ++i)
        noteDropdown.addItem(kNoteEntries[i].label, i + 1);
    noteDropdown.onChange = [this](int id)
    {
        const int i = id - 1;
        if (i < 0 || i >= kNoteEntryCount) return;
        noteValue = kNoteEntries[i].nv;
        noteMod   = kNoteEntries[i].mod;
        fire();
    };
    addAndMakeVisible(noteDropdown);

    multiplier.setShowStepButtons(false);
    multiplier.setLabelInline(true);
    multiplier.onChange = [this](int) { fire(); };
    addAndMakeVisible(multiplier);
}

void NoteLengthControl::setLength(NoteValue nv, NoteMod mod, int mult)
{
    noteValue = nv;
    noteMod   = mod;
    noteDropdown.setSelectedId(noteToId(nv, mod));
    multiplier.setValue(juce::jlimit(1, kMaxMultiplier, mult));
}

void NoteLengthControl::fire()
{
    if (onChange) onChange(noteValue, noteMod, multiplier.getValue());
}

void NoteLengthControl::setMetalStyle(bool metal)
{
    noteDropdown.setLcdStyle(metal);
    label.setColour(juce::Label::textColourId,
                    MuLookAndFeel::colour(metal ? MuLookAndFeel::labelText : MuLookAndFeel::mutedText));
}

void NoteLengthControl::resized()
{
    using mu_ui::s;
    auto r = getLocalBounds();
    label       .setBounds(r.removeFromLeft(s(kLabelW)));    r.removeFromLeft(s(kGap));
    noteDropdown.setBounds(r.removeFromLeft(s(kDropdownW))); r.removeFromLeft(s(kGap));
    multiplier  .setBounds(r.removeFromLeft(s(kMultW)));
}
