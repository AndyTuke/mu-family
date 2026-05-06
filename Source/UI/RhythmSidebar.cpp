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
    const int prevCount = (int)items.size();

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
        item->setPlayState(&proc.rhythmPlayState[i], &proc.beatFraction, &proc.sequencerPlaying);
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

    // If the user added a rhythm (prevCount > 0 rules out initial construction),
    // animate the new item sliding in from below.
    if (n > prevCount && prevCount > 0)
        layoutItems(true, n - 1);
}

void RhythmSidebar::layoutItems(bool animate, int newItemIndex)
{
    const int w = itemContainer.getWidth();
    for (int i = 0; i < (int)items.size(); i++)
    {
        const juce::Rectangle<int> target(0, i * kItemH, w, kItemH);
        if (animate && i == newItemIndex)
        {
            // Start below and transparent, fade + slide into the correct position
            items[i]->setAlpha(0.0f);
            items[i]->setBounds(0, (i + 1) * kItemH, w, kItemH);
            animator.animateComponent(items[i].get(), target, 1.0f, 120, false, 0.0, 1.0);
        }
        else if (animate)
        {
            animator.animateComponent(items[i].get(), target, 1.0f, 80, false, 1.0, 1.0);
        }
        else
        {
            items[i]->setBounds(target);
        }
    }
}

void RhythmSidebar::repaintItems()
{
    for (auto& item : items)
        item->repaint();
    itemContainer.repaint();
    repaint();
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
