#include "SaveDialog.h"

SaveDialog::SaveDialog()
{
    nameEditor.setTextToShowWhenEmpty("Preset name", juce::Colours::grey);
    nameEditor.setFont(juce::Font(juce::FontOptions{}.withHeight(13.0f)));
    addAndMakeVisible(nameEditor);

    descEditor.setTextToShowWhenEmpty("Description (optional)", juce::Colours::grey);
    descEditor.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    addAndMakeVisible(descEditor);

    addAndMakeVisible(categoryDropdown);

    newCategoryEditor.setTextToShowWhenEmpty("New category name", juce::Colours::grey);
    newCategoryEditor.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    newCategoryEditor.setVisible(false);
    addAndMakeVisible(newCategoryEditor);

    categoryDropdown.onChange = [this](int id)
    {
        const bool isNew = (id == knownCategories.size() + 2);
        newCategoryEditor.setVisible(isNew);
        resized();
        if (isNew) newCategoryEditor.grabKeyboardFocus();
    };

    addAndMakeVisible(embedSamplesToggle);

    saveBtn.onClick = [this]
    {
        const auto name = nameEditor.getText().trim();
        if (name.isEmpty()) return;
        if (onSave)
            onSave(name, descEditor.getText().trim(), resolveCategory(),
                   embedSamplesToggle.getToggleState());
    };
    addAndMakeVisible(saveBtn);

    cancelBtn.onClick = [this] { if (onCancel) onCancel(); };
    addAndMakeVisible(cancelBtn);

    // Populate with just "All" + "New..." until setKnownCategories() is called.
    setKnownCategories({});
}

void SaveDialog::setKnownCategories(const juce::StringArray& cats)
{
    knownCategories = cats;
    categoryDropdown.clear();
    categoryDropdown.addItem("Uncategorised", 1);
    for (int i = 0; i < cats.size(); ++i)
        categoryDropdown.addItem(cats[i], i + 2);
    categoryDropdown.addItem("New...", cats.size() + 2);
    categoryDropdown.setSelectedId(1, false);
    newCategoryEditor.setVisible(false);
    newCategoryEditor.clear();
}

juce::String SaveDialog::resolveCategory() const
{
    const int id    = categoryDropdown.getSelectedId();
    const int newId = knownCategories.size() + 2;
    if (id == newId)
    {
        const auto t = newCategoryEditor.getText().trim();
        return t.isEmpty() ? "Uncategorised" : t;
    }
    if (id >= 2 && id - 2 < knownCategories.size())
        return knownCategories[id - 2];
    return "Uncategorised";
}

void SaveDialog::visibilityChanged()
{
    if (isVisible())
    {
        nameEditor.clear();
        descEditor.clear();
        categoryDropdown.setSelectedId(1, false);
        newCategoryEditor.setVisible(false);
        newCategoryEditor.clear();
        embedSamplesToggle.setToggleState(false, juce::dontSendNotification);
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

    int y = cardY + 44;  // leave room for title
    const int fieldW = kCardW - 48;
    const int fieldX = cardX + 24;

    nameEditor    .setBounds(fieldX, y, fieldW, 28);  y += 36;
    descEditor    .setBounds(fieldX, y, fieldW, 28);  y += 36;
    categoryDropdown.setBounds(fieldX, y, fieldW, 28); y += 34;

    if (newCategoryEditor.isVisible())
    {
        newCategoryEditor.setBounds(fieldX, y, fieldW, 24);
        y += 30;
    }

    embedSamplesToggle.setBounds(fieldX, y, fieldW, 24);

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
    g.drawText("Save Preset", cardX + 24, cardY + 14, kCardW - 48, 20,
               juce::Justification::centredLeft, false);
}
