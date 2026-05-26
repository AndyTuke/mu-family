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
    listBox.setRowHeight(mu_ui::s(24));
    listBox.setColour(juce::ListBox::backgroundColourId,
                      MuLookAndFeel::colour(MuLookAndFeel::panelBackground));
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
        const juce::String pattern = "*." + fileExtension;
        for (const auto& f : dir.findChildFiles(juce::File::findFiles, false, pattern))
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
                      return a.name.compareIgnoreCase(b.name) < 0;
                  });
    }

    // Build sorted list of unique categories from preset metadata + categories.txt.
    knownCategories.clear();
    for (const auto& p : allPresets)
        if (p.category.isNotEmpty() && p.category != "All" && !knownCategories.contains(p.category))
            knownCategories.add(p.category);
    {
        juce::StringArray fromFile;
        dir.getChildFile("categories.txt").readLines(fromFile);
        for (const auto& c : fromFile)
            if (c.isNotEmpty() && c != "All" && !knownCategories.contains(c, false))
                knownCategories.add(c);
    }
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
    using Id = MuLookAndFeel::ColourIds;
    using mu_ui::s;
    using mu_ui::sf;

    if (selected)
    {
        g.setColour(MuLookAndFeel::colour(Id::segmentActiveBg));
        g.fillRect(0, 0, w, h);
    }

    if (row >= (int)filteredIndices.size()) return;
    const auto& info = allPresets[filteredIndices[row]];

    g.setColour(MuLookAndFeel::colour(selected ? Id::headingText : Id::labelText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(12.0f))));
    g.drawText(info.name, s(8), 0, w / 2, h, juce::Justification::centredLeft, true);

    g.setColour(MuLookAndFeel::colour(Id::mutedText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(10.0f))));
    g.drawText(info.category, w / 2, 0, w / 4, h,
               juce::Justification::centredLeft, true);
    g.drawText(info.description, w * 3 / 4, 0, w / 4 - s(4), h,
               juce::Justification::centredRight, true);

    g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBorder));
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
    using mu_ui::s;
    const int w = getWidth();
    const int h = getHeight();
    const int pad = s(kPad);
    const int topH = s(kTopBarH);
    const int botH = s(kBotBarH);

    // Top bar: search + category filter
    int x = pad;
    const int searchW   = s(160);
    const int categoryW = w - searchW - pad * 3;
    searchBox    .setBounds(x, pad, searchW, topH - pad * 2);
    categoryFilter.setBounds(x + searchW + pad, pad, categoryW, topH - pad * 2);

    // Bottom bar
    const int btnW = s(80);
    closeBtn.setBounds(pad, h - botH + pad, btnW, botH - pad * 2);
    loadBtn .setBounds(w - pad - btnW, h - botH + pad, btnW, botH - pad * 2);

    // List box fills the middle
    listBox.setBounds(0, topH, w, h - topH - botH);
}

void PresetBrowser::paint(juce::Graphics& g)
{
    using Id = MuLookAndFeel::ColourIds;
    using mu_ui::s;
    g.setColour(MuLookAndFeel::colour(Id::panelBackground));
    g.fillAll();

    const int topH = s(kTopBarH);
    const int botH = s(kBotBarH);
    g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawLine(0.0f, (float)topH, (float)getWidth(), (float)topH, 0.5f);
    g.drawLine(0.0f, (float)(getHeight() - botH), (float)getWidth(),
               (float)(getHeight() - botH), 0.5f);
}
