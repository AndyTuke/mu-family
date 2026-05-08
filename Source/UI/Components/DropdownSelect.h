#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "MuClidLookAndFeel.h"

// Styled wrapper around juce::ComboBox.
class DropdownSelect : public juce::Component
{
public:
    std::function<void(int id)> onChange;

    DropdownSelect();

    void addItem(const juce::String& text, int id);
    void addSectionHeading(const juce::String& text);
    void setSelectedId(int id, bool notify = false);
    int  getSelectedId() const;
    void clear();

    void resized() override;

private:
    juce::ComboBox combo;
};
