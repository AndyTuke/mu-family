#include "DropdownSelect.h"

DropdownSelect::DropdownSelect()
{
    combo.setJustificationType(juce::Justification::centredLeft);
    combo.onChange = [this] { if (onChange) onChange(combo.getSelectedId()); };
    addAndMakeVisible(combo);
}

void DropdownSelect::addItem(const juce::String& text, int id)
{
    combo.addItem(text, id);
}

void DropdownSelect::addSectionHeading(const juce::String& text)
{
    combo.addSectionHeading(text);
}

void DropdownSelect::setSelectedId(int id, bool notify)
{
    combo.setSelectedId(id, notify ? juce::sendNotification : juce::dontSendNotification);
}

int DropdownSelect::getSelectedId() const
{
    return combo.getSelectedId();
}

void DropdownSelect::clear()
{
    combo.clear(juce::dontSendNotification);
}

void DropdownSelect::resized()
{
    combo.setBounds(getLocalBounds());
}
