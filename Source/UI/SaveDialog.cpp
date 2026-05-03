#include "SaveDialog.h"

SaveDialog::SaveDialog()
{
    nameEditor.setTextToShowWhenEmpty("Preset name", juce::Colours::grey);
    nameEditor.setFont(juce::Font(juce::FontOptions{}.withHeight(13.0f)));
    addAndMakeVisible(nameEditor);

    descEditor.setTextToShowWhenEmpty("Description (optional)", juce::Colours::grey);
    descEditor.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    addAndMakeVisible(descEditor);

    addAndMakeVisible(categoryControl);

    saveBtn.onClick = [this]
    {
        const auto name = nameEditor.getText().trim();
        if (name.isEmpty()) return;
        if (onSave)
            onSave(name, descEditor.getText().trim(),
                   categoryControl.getSelectedIndex() == 0 ? "All"
                   : categoryControl.getSelectedIndex() == 1 ? "Techno"
                   : categoryControl.getSelectedIndex() == 2 ? "Perc"
                   : categoryControl.getSelectedIndex() == 3 ? "Ambient"
                   : "Experimental");
    };
    addAndMakeVisible(saveBtn);

    cancelBtn.onClick = [this] { if (onCancel) onCancel(); };
    addAndMakeVisible(cancelBtn);
}

void SaveDialog::visibilityChanged()
{
    if (isVisible())
    {
        nameEditor.clear();
        descEditor.clear();
        categoryControl.setSelectedIndex(0, false);
        nameEditor.grabKeyboardFocus();
    }
}

void SaveDialog::mouseDown(const juce::MouseEvent& e)
{
    const int w = getWidth();
    const int h = getHeight();
    const int cardX = (w - kCardW) / 2;
    const int cardY = (h - kCardH) / 2;
    const juce::Rectangle<int> card { cardX, cardY, kCardW, kCardH };

    if (!card.contains(e.getPosition()))
        if (onCancel) onCancel();
}

void SaveDialog::resized()
{
    const int w = getWidth();
    const int h = getHeight();
    const int cardX = (w - kCardW) / 2;
    const int cardY = (h - kCardH) / 2;

    int y = cardY + 24;
    const int fieldW = kCardW - 48;
    const int fieldX = cardX + 24;

    nameEditor.setBounds(fieldX, y, fieldW, 28);           y += 36;
    descEditor.setBounds(fieldX, y, fieldW, 28);           y += 36;
    categoryControl.setBounds(fieldX, y, fieldW, 28);      y += 44;

    const int btnW = 80;
    const int btnY = cardY + kCardH - 44;
    cancelBtn.setBounds(fieldX,              btnY, btnW, 28);
    saveBtn  .setBounds(fieldX + fieldW - btnW, btnY, btnW, 28);
}

void SaveDialog::paint(juce::Graphics& g)
{
    using Id = MuClidLookAndFeel::ColourIds;

    g.setColour(juce::Colour(0xe6000000));
    g.fillAll();

    const int w = getWidth();
    const int h = getHeight();
    const int cardX = (w - kCardW) / 2;
    const int cardY = (h - kCardH) / 2;

    g.setColour(MuClidLookAndFeel::colour(Id::panelBackground));
    g.fillRoundedRectangle((float)cardX, (float)cardY, (float)kCardW, (float)kCardH, 8.0f);

    g.setColour(MuClidLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawRoundedRectangle((float)cardX, (float)cardY, (float)kCardW, (float)kCardH, 8.0f, 1.0f);

    g.setColour(MuClidLookAndFeel::colour(Id::headingText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(14.0f)));
    g.drawText("Save Preset", cardX + 24, cardY + 24 - 20, kCardW - 48, 20,
               juce::Justification::centredLeft, false);
}
