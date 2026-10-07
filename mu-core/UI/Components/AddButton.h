#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "MuLookAndFeel.h"

// "+ label" button (add a layer / target / assignment). In metal style it is drawn like
// every other family button (MuLookAndFeel button look); flat style keeps a dashed border.
// Callers set `onClick` (juce::Button's) — e.g. to open a PopupMenu.
class AddButton : public juce::TextButton
{
public:
    explicit AddButton(const juce::String& label);

    // Dim + drop the pointing-hand cursor when disabled (e.g. demo channel cap reached).
    void enablementChanged() override;

protected:
    void paintButton(juce::Graphics&, bool isOver, bool isDown) override;
};
