#include "SaveDialog.h"

SaveDialog::SaveDialog()
{
    logoImage = juce::ImageCache::getFromMemory(BinaryData::muclid_png,
                                                BinaryData::muclid_pngSize);

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

    saveAsDefaultToggle.onClick = [this] { updateDefaultModeState(); };
    addAndMakeVisible(saveAsDefaultToggle);

    saveBtn.onClick = [this]
    {
        // in Save-as-Default mode the filename is hard-coded to _default.muClid,
        // so the name field is disabled (see updateDefaultModeState) and empty by
        // design. Don't gate the click on a non-empty name in that mode.
        const auto name = nameEditor.getText().trim();
        if (!isSaveAsDefault() && name.isEmpty()) return;
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
        if (pendingDefaultName.isNotEmpty())
        {
            nameEditor.setText(pendingDefaultName, false);
            nameEditor.selectAll();
            pendingDefaultName.clear();
        }
        else
        {
            nameEditor.clear();
        }
        descEditor.clear();
        categoryDropdown.setSelectedId(1, false);
        if (pendingDefaultCategory.isNotEmpty())
        {
            for (int i = 0; i < knownCategories.size(); ++i)
            {
                if (knownCategories[i].equalsIgnoreCase(pendingDefaultCategory))
                {
                    categoryDropdown.setSelectedId(i + 2, false);
                    break;
                }
            }
            pendingDefaultCategory.clear();
        }
        newCategoryEditor.setVisible(false);
        newCategoryEditor.clear();
        embedSamplesToggle.setToggleState(pendingDefaultEmbed, juce::dontSendNotification);
        pendingDefaultEmbed = false;
        saveAsDefaultToggle.setToggleState(false, juce::dontSendNotification);
        updateDefaultModeState();
        nameEditor.grabKeyboardFocus();
    }
}

void SaveDialog::updateDefaultModeState()
{
    const bool isDefault = saveAsDefaultToggle.getToggleState();
    nameEditor        .setEnabled(!isDefault);
    descEditor        .setEnabled(!isDefault);
    categoryDropdown  .setEnabled(!isDefault);
    newCategoryEditor .setEnabled(!isDefault);
    resized();
}

void SaveDialog::mouseDown(const juce::MouseEvent& e)
{
    using mu_ui::s;
    const int w = getWidth();
    const int h = getHeight();
    const int cardW = s(kCardW);
    const int cardH = s(kCardH);
    const int cardX = (w - cardW) / 2;
    const int cardY = (h - cardH) / 2;
    const juce::Rectangle<int> card { cardX, cardY, cardW, cardH };

    if (!card.contains(e.getPosition()))
        if (onCancel) onCancel();
}

void SaveDialog::resized()
{
    using mu_ui::s;
    const int w = getWidth();
    const int h = getHeight();
    const int cardW = s(kCardW);
    const int cardH = s(kCardH);
    const int cardX = (w - cardW) / 2;
    const int cardY = (h - cardH) / 2;

    int y = cardY + s(116);  // leave room for 96px logo + padding
    const int fieldW = cardW - s(48);
    const int fieldX = cardX + s(24);

    const bool isDefault = saveAsDefaultToggle.getToggleState();

    nameEditor    .setBounds(fieldX, y, fieldW, s(28));  y += s(36);
    descEditor    .setBounds(fieldX, y, fieldW, s(28));  y += s(36);
    categoryDropdown.setBounds(fieldX, y, fieldW, s(28)); y += s(34);

    if (newCategoryEditor.isVisible() && !isDefault)
    {
        newCategoryEditor.setBounds(fieldX, y, fieldW, s(24));
        y += s(30);
    }

    embedSamplesToggle .setBounds(fieldX, y, fieldW / 2, s(24));
    saveAsDefaultToggle.setBounds(fieldX + fieldW / 2, y, fieldW / 2, s(24));

    const int btnW = s(80);
    const int btnY = cardY + cardH - s(44);
    cancelBtn.setBounds(fieldX,                   btnY, btnW, s(28));
    saveBtn  .setBounds(fieldX + fieldW - btnW,   btnY, btnW, s(28));
}

void SaveDialog::paint(juce::Graphics& g)
{
    using Id = MuClidLookAndFeel::ColourIds;
    using mu_ui::s;
    using mu_ui::sf;

    g.setColour(MuClidLookAndFeel::colour(Id::backgroundModalDim));
    g.fillAll();

    const int w = getWidth();
    const int h = getHeight();
    const int cardW = s(kCardW);
    const int cardH = s(kCardH);
    const int cardX = (w - cardW) / 2;
    const int cardY = (h - cardH) / 2;

    g.setColour(MuClidLookAndFeel::colour(Id::panelBackground));
    g.fillRoundedRectangle((float)cardX, (float)cardY, (float)cardW, (float)cardH, sf(8.0f));

    g.setColour(MuClidLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawRoundedRectangle((float)cardX, (float)cardY, (float)cardW, (float)cardH, sf(8.0f), 1.0f);

    // Logo on the right side of the header, title on the left
    if (logoImage.isValid())
    {
        const int logoSz = s(96);
        g.drawImage(logoImage, cardX + s(16), cardY + s(12), logoSz, logoSz,
                    0, 0, logoImage.getWidth(), logoImage.getHeight());
    }
    g.setColour(MuClidLookAndFeel::colour(Id::headingText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(14.0f))));
    g.drawText("Save Preset", cardX + s(120), cardY + s(40), cardW - s(136), s(20),
               juce::Justification::centredLeft, false);
}
