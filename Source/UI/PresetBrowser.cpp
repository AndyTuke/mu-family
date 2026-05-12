#include "PresetBrowser.h"

PresetBrowser::PresetBrowser()
{
    searchBox.setTextToShowWhenEmpty("Search presets...", juce::Colours::grey);
    searchBox.onTextChange = [this] { applyFilter(); };
    addAndMakeVisible(searchBox);

    categoryFilter.onChange = [this](int) { applyFilter(); };
    categoryFilter.addItem("All", 1);
    addAndMakeVisible(categoryFilter);

    listBox.setModel(this);
    listBox.setRowHeight(24);
    listBox.setColour(juce::ListBox::backgroundColourId,
                      MuClidLookAndFeel::colour(MuClidLookAndFeel::panelBackground));
    addAndMakeVisible(listBox);

    loadBtn.onClick  = [this] { loadSelectedPreset(); };
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible(loadBtn);
    addAndMakeVisible(closeBtn);
}

void PresetBrowser::refresh(const juce::File& dir)
{
    presetsDir = dir;
    allPresets.clear();

    if (dir.isDirectory())
    {
        for (const auto& f : dir.findChildFiles(juce::File::findFiles, false, "*.muclid"))
        {
            PresetInfo info;
            info.file = f;
            info.name = f.getFileNameWithoutExtension();

            if (auto xml = juce::parseXML(f))
            {
                auto state = juce::ValueTree::fromXml(*xml);
                info.category    = state.getProperty("presetCategory",    "All").toString();
                info.description = state.getProperty("presetDescription", "").toString();
            }

            allPresets.push_back(std::move(info));
        }

        std::sort(allPresets.begin(), allPresets.end(),
                  [](const PresetInfo& a, const PresetInfo& b) {
                      return a.name.compareIgnoreCase(b.name) < 0;   // #251
                  });
    }

    // Build sorted list of unique categories, then repopulate the filter dropdown.
    knownCategories.clear();
    for (const auto& p : allPresets)
        if (p.category.isNotEmpty() && p.category != "All" && !knownCategories.contains(p.category))
            knownCategories.add(p.category);
    knownCategories.sort(false);

    categoryFilter.clear();
    categoryFilter.addItem("All", 1);
    for (int i = 0; i < knownCategories.size(); ++i)
        categoryFilter.addItem(knownCategories[i], i + 2);
    categoryFilter.setSelectedId(1, false);

    selectedRow = -1;
    applyFilter();
}

void PresetBrowser::applyFilter()
{
    filteredIndices.clear();
    const juce::String query     = searchBox.getText().trim().toLowerCase();
    const int          catId     = categoryFilter.getSelectedId();
    // ID 1 = "All", ID 2+ = knownCategories[id-2]
    const juce::String filterCat = (catId >= 2 && catId - 2 < knownCategories.size())
                                   ? knownCategories[catId - 2] : juce::String();

    for (int i = 0; i < (int)allPresets.size(); ++i)
    {
        const auto& p = allPresets[i];
        if (filterCat.isNotEmpty() && p.category != filterCat) continue;
        if (query.isNotEmpty() && !p.name.toLowerCase().contains(query)) continue;
        filteredIndices.push_back(i);
    }

    selectedRow = -1;
    listBox.updateContent();
    listBox.repaint();
}

void PresetBrowser::loadSelectedPreset()
{
    if (selectedRow < 0 || selectedRow >= (int)filteredIndices.size()) return;
    const auto& info = allPresets[filteredIndices[selectedRow]];
    if (onLoadPreset) onLoadPreset(info.file);
    if (onClose)      onClose();
}

// ── ListBoxModel ─────────────────────────────────────────────────────────────

int PresetBrowser::getNumRows() { return (int)filteredIndices.size(); }

void PresetBrowser::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected)
{
    using Id = MuClidLookAndFeel::ColourIds;

    if (selected)
    {
        g.setColour(MuClidLookAndFeel::colour(Id::segmentActiveBg));
        g.fillRect(0, 0, w, h);
    }

    if (row >= (int)filteredIndices.size()) return;
    const auto& info = allPresets[filteredIndices[row]];

    g.setColour(MuClidLookAndFeel::colour(selected ? Id::headingText : Id::labelText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(12.0f)));
    g.drawText(info.name, 8, 0, w / 2, h, juce::Justification::centredLeft, true);

    g.setColour(MuClidLookAndFeel::colour(Id::mutedText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
    g.drawText(info.category, w / 2, 0, w / 4, h,
               juce::Justification::centredLeft, true);
    g.drawText(info.description, w * 3 / 4, 0, w / 4 - 4, h,
               juce::Justification::centredRight, true);

    g.setColour(MuClidLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawLine(0.0f, (float)(h - 1), (float)w, (float)(h - 1), 0.5f);
}

void PresetBrowser::listBoxItemClicked(int row, const juce::MouseEvent& e)
{
    selectedRow = row;
    if (e.mods.isRightButtonDown())
    {
        juce::PopupMenu menu;
        menu.addItem(1, "Load");
        menu.addItem(2, "Delete");
        menu.showMenuAsync(juce::PopupMenu::Options{}, [this, row](int result)
        {
            if (result == 1) loadSelectedPreset();
            else if (result == 2 && row < (int)filteredIndices.size())
            {
                allPresets[filteredIndices[row]].file.deleteFile();
                applyFilter();
            }
        });
    }
}

void PresetBrowser::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
{
    selectedRow = row;
    loadSelectedPreset();
}

// ── Layout ────────────────────────────────────────────────────────────────────

void PresetBrowser::resized()
{
    const int w = getWidth();
    const int h = getHeight();

    // Top bar: search + category filter
    int x = kPad;
    const int searchW   = 160;
    const int categoryW = w - searchW - kPad * 3;
    searchBox    .setBounds(x, kPad, searchW, kTopBarH - kPad * 2);
    categoryFilter.setBounds(x + searchW + kPad, kPad, categoryW, kTopBarH - kPad * 2);

    // Bottom bar
    const int btnW = 80;
    closeBtn.setBounds(kPad, h - kBotBarH + kPad, btnW, kBotBarH - kPad * 2);
    loadBtn .setBounds(w - kPad - btnW, h - kBotBarH + kPad, btnW, kBotBarH - kPad * 2);

    // List box fills the middle
    listBox.setBounds(0, kTopBarH, w, h - kTopBarH - kBotBarH);
}

void PresetBrowser::paint(juce::Graphics& g)
{
    using Id = MuClidLookAndFeel::ColourIds;
    g.setColour(MuClidLookAndFeel::colour(Id::panelBackground));
    g.fillAll();

    g.setColour(MuClidLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawLine(0.0f, (float)kTopBarH, (float)getWidth(), (float)kTopBarH, 0.5f);
    g.drawLine(0.0f, (float)(getHeight() - kBotBarH), (float)getWidth(),
               (float)(getHeight() - kBotBarH), 0.5f);
}
