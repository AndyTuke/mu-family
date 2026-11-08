#include "DropdownSelect.h"

DropdownSelect::DropdownSelect()
{
    combo.setJustificationType(juce::Justification::centredLeft);
    combo.onChange = [this]
    {
        setDisplayOverride({}, false);   // a pick takes over from a shown clip name
        if (onChange) onChange(combo.getSelectedId());
        if (statusName.isNotEmpty()) mu_ui::reportStatus(*this, statusName, combo.getText());
    };
    addAndMakeVisible(combo);
}

void DropdownSelect::addItem(const juce::String& text, int id)
{
    if (multiSelect) { multiItems.push_back({ id, text }); refreshMultiSummary(); return; }
    combo.addItem(text, id);
}

// ── Multi-select mode ──────────────────────────────────────────────────────────────────────

void DropdownSelect::setMultiSelect(bool on)
{
    multiSelect = on;
    combo.popupOverride = on ? std::function<void()>([this] { showMultiPopup(); }) : std::function<void()>();
    refreshMultiSummary();
}

void DropdownSelect::setTicked(int id, bool ticked)
{
    for (auto& it : multiItems) if (it.id == id) it.ticked = ticked;
    refreshMultiSummary();
}

bool DropdownSelect::isTicked(int id) const
{
    for (const auto& it : multiItems) if (it.id == id) return it.ticked;
    return false;
}

void DropdownSelect::setItemEnabled(int id, bool enabled)
{
    for (auto& it : multiItems) if (it.id == id) it.enabled = enabled;
    refreshMultiSummary();
}

void DropdownSelect::toggle(int id)
{
    for (auto& it : multiItems)
        if (it.id == id && it.enabled)
        {
            it.ticked = ! it.ticked;
            refreshMultiSummary();
            if (onToggle) onToggle(id, it.ticked);
            if (statusName.isNotEmpty()) mu_ui::reportStatus(*this, statusName, combo.getText());
            return;
        }
}

// The box's text: the ticked items joined by commas, All when every enabled item is ticked, None when none is.
void DropdownSelect::refreshMultiSummary()
{
    if (! multiSelect) return;
    juce::StringArray ticked;
    int enabledCount = 0;
    for (const auto& it : multiItems)
    {
        if (it.enabled) ++enabledCount;
        if (it.ticked && it.enabled) ticked.add(it.text);
    }
    const juce::String text = ticked.isEmpty() ? juce::String("None")
                            : (ticked.size() == enabledCount && enabledCount > 1) ? juce::String("All")
                            : ticked.joinIntoString(", ");
    combo.setSelectedId(0, juce::dontSendNotification);
    combo.setText(text, juce::dontSendNotification);
}

// The popup: one ticked row per item. A pick toggles it and the popup comes back so more can be ticked.
void DropdownSelect::showMultiPopup()
{
    juce::PopupMenu menu;
    for (const auto& it : multiItems)
        menu.addItem(juce::PopupMenu::Item(it.text).setID(it.id).setTicked(it.ticked).setEnabled(it.enabled));
    juce::Component::SafePointer<DropdownSelect> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&combo).withMinimumWidth(combo.getWidth()),
                       [safe](int picked)
                       {
                           if (safe == nullptr || picked <= 0) return;
                           safe->toggle(picked);
                           juce::MessageManager::callAsync([safe] { if (safe != nullptr) safe->showMultiPopup(); });
                       });
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
    multiItems.clear();
    refreshMultiSummary();
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
