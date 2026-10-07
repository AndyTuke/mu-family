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

void DropdownSelect::setPlaceholderText(const juce::String& text)
{
    combo.setTextWhenNothingSelected(text);
    combo.setTextWhenNoChoicesAvailable(text);
}

void DropdownSelect::setLcdStyle(bool lcd)
{
    combo.getProperties().set("muLcd", lcd);
    if (lcd) combo.setColour(juce::ComboBox::textColourId, MuLookAndFeel::lcdLitColour(combo));
    else     combo.removeColour(juce::ComboBox::textColourId);
    combo.lookAndFeelChanged();   // re-run positionComboBoxText for the font
    combo.repaint();
}

void DropdownSelect::setLcdColour(juce::Colour c)
{
    if (c.isTransparent()) combo.getProperties().remove("muLcdColour");
    else                   combo.getProperties().set("muLcdColour", (juce::int64) c.getARGB());
    if (MuLookAndFeel::lcdCombo(combo))
        combo.setColour(juce::ComboBox::textColourId, MuLookAndFeel::lcdLitColour(combo));
    combo.lookAndFeelChanged();   // positionComboBoxText re-colours the value label
    combo.repaint();
}

void DropdownSelect::resized()
{
    combo.setBounds(getLocalBounds());
}
