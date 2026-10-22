#include "DropdownSelect.h"

DropdownSelect::DropdownSelect()
{
    combo.setJustificationType(juce::Justification::centredLeft);
    combo.onChange = [this]
    {
        setDisplayOverride({}, false);   // a pick takes over from a shown clip name
        if (onChange) onChange(combo.getSelectedId());
    };
    addAndMakeVisible(combo);
}

void DropdownSelect::addItem(const juce::String& text, int id)
{
    combo.addItem(text, id);
}

void DropdownSelect::setDisplayOverride(const juce::String& text, bool queued)
{
    const bool wasQueued = isDisplayQueued();
    const bool had       = hasDisplayOverride();
    overrideText   = text;
    overrideQueued = queued && text.isNotEmpty();

    if (had && text.isEmpty())
        combo.setSelectedId(combo.getSelectedId(), juce::dontSendNotification);   // back to the item or the placeholder
    applyOverride();
    if (wasQueued != isDisplayQueued() || had != hasDisplayOverride())
        refreshTextColour();
}

// Shows the override text when no item is chosen (id 0); a chosen item always wins.
void DropdownSelect::applyOverride()
{
    if (overrideText.isNotEmpty() && combo.getSelectedId() == 0)
        combo.setText(overrideText, juce::dontSendNotification);
}

void DropdownSelect::refreshTextColour()
{
    combo.getProperties().set("muLcdQueued", isDisplayQueued());
    if (MuLookAndFeel::lcdCombo(combo))
        combo.setColour(juce::ComboBox::textColourId, MuLookAndFeel::lcdLitColour(combo));
    else if (isDisplayQueued())
        combo.setColour(juce::ComboBox::textColourId, MuLookAndFeel::colour(MuLookAndFeel::queuedText));
    else
        combo.removeColour(juce::ComboBox::textColourId);
    combo.lookAndFeelChanged();   // positionComboBoxText re-colours the value label
    combo.repaint();
}

void DropdownSelect::addSectionHeading(const juce::String& text)
{
    combo.addSectionHeading(text);
}

void DropdownSelect::setSelectedId(int id, bool notify)
{
    if (id > 0 && hasDisplayOverride())
        setDisplayOverride({}, false);   // an item chosen in code takes over too
    combo.setSelectedId(id, notify ? juce::sendNotification : juce::dontSendNotification);
    applyOverride();
}

int DropdownSelect::getSelectedId() const
{
    return combo.getSelectedId();
}

void DropdownSelect::addItem(const juce::String& text, int id, juce::Colour textColour)
{
    combo.getRootMenu()->addItem(juce::PopupMenu::Item(text).setID(id).setColour(textColour));
}

void DropdownSelect::clear()
{
    combo.clear(juce::dontSendNotification);
    applyOverride();
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
