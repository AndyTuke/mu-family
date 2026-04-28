#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../../../Source/Sequencer/ControlSequence.h"

// Note grid selector: 1/1 1/2 1/4 1/8 1/16 1/32 + triplet/dotted toggles.
// Used for loop length, delay time, modulator length.
class TimeSelector : public juce::Component
{
public:
    std::function<void(NoteValue, NoteMod)> onChange;

    TimeSelector();

    void setSelection(NoteValue nv, NoteMod mod, bool notify = false);
    NoteValue getNoteValue() const noexcept { return noteValue; }
    NoteMod   getNoteMod()   const noexcept { return noteMod; }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    NoteValue noteValue = NoteValue::Quarter;
    NoteMod   noteMod   = NoteMod::None;

    static constexpr int kNoteCount = 6;
    static const char* kNoteLabels[kNoteCount];
    static const NoteValue kNoteValues[kNoteCount];

    juce::Rectangle<int> noteBounds[kNoteCount];
    juce::Rectangle<int> tripletBounds, dottedBounds;
};
