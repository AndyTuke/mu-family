#include "RhythmSidebar.h"

RhythmSidebar::RhythmSidebar(PluginProcessor& p)
    : proc(p)
{
    viewport.setViewedComponent(&itemContainer, false);
    viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport);
    addAndMakeVisible(addButton);

    addButton.onClick = [this] { if (onAddRhythm) onAddRhythm(); };
    refreshItems();
}

void RhythmSidebar::refreshItems()
{
    for (auto& item : items)
        itemContainer.removeChildComponent(item.get());
    items.clear();

    const int n = proc.getNumRhythms();
    for (int i = 0; i < n; i++)
    {
        auto item = std::make_unique<SidebarItem>(i);
        const Rhythm& r = proc.getRhythm(i);
        juce::Colour col = MuClidLookAndFeel::rhythmPalette[r.colourIndex % 30];
        item->setRhythm(&r, col);
        item->setSelected(i == selectedIndex);
        item->onSelected = [this](int idx)
        {
            setSelectedIndex(idx);
            if (onRhythmSelected) onRhythmSelected(idx);
        };
        itemContainer.addAndMakeVisible(item.get());
        items.push_back(std::move(item));
    }

    itemContainer.setSize(kWidth, (int)items.size() * kItemH);
    resized();
}

void RhythmSidebar::setSelectedIndex(int i)
{
    selectedIndex = i;
    for (int j = 0; j < (int)items.size(); j++)
        items[j]->setSelected(j == i);
}

void RhythmSidebar::paint(juce::Graphics& g)
{
    using Id = MuClidLookAndFeel::ColourIds;
    g.setColour(MuClidLookAndFeel::colour(Id::sidebarBackground));
    g.fillAll();

    // Right border
    g.setColour(MuClidLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawLine((float)(getWidth() - 1), 0.0f, (float)(getWidth() - 1), (float)getHeight(), 1.0f);
}

void RhythmSidebar::resized()
{
    const int w = getWidth();
    const int h = getHeight();
    const int addBtnY = h - kAddBtnH - 4;

    addButton.setBounds(4, addBtnY, w - 8, kAddBtnH);
    viewport.setBounds(0, 0, w, addBtnY - 2);
    itemContainer.setSize(w, juce::jmax(1, (int)items.size() * kItemH));

    for (int i = 0; i < (int)items.size(); i++)
        items[i]->setBounds(0, i * kItemH, w, kItemH);
}
