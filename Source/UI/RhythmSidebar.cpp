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
    startTimerHz(5); // poll pending swap states for all items
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
        item->onDragStart = [this](int idx, const juce::MouseEvent& e) { onItemDragStart(idx, e); };
        item->onDragMove  = [this](int idx, const juce::MouseEvent& e) { onItemDragMove (idx, e); };
        item->onDragEnd   = [this](int idx, const juce::MouseEvent& e) { onItemDragEnd  (idx, e); };
        item->onCancelPendingSwap = [this](int idx) { proc.cancelStagedSwap(idx); };
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

void RhythmSidebar::paintOverChildren(juce::Graphics& g)
{
    if (dragPhase != DragPhase::Dragging) return;

    // Convert itemContainer-local coords to sidebar-local coords (viewport accounts for scroll).
    const int viewX = viewport.getX();
    const int viewY = viewport.getY();
    const int scrollY = viewport.getViewPositionY();

    if (dragTargetIndex >= 0)
    {
        const int lineY = viewY + dragTargetIndex * kItemH - scrollY;
        if (lineY >= viewY - 1 && lineY <= viewY + viewport.getHeight())
        {
            g.setColour(juce::Colours::white);
            g.fillRect(viewX + 2, lineY - 1, viewport.getWidth() - 4, 2);
        }
    }

    if (dragGhostImage.isValid())
    {
        const int gx = viewX + dragGhostBounds.getX();
        const int gy = viewY + dragGhostBounds.getY() - scrollY;
        g.setOpacity(0.75f);
        g.drawImageAt(dragGhostImage, gx, gy);
        g.setOpacity(1.0f);
    }
}

void RhythmSidebar::onItemDragStart(int sourceIndex, const juce::MouseEvent& e)
{
    if (sourceIndex < 0 || sourceIndex >= (int)items.size()) return;

    dragPhase = DragPhase::Dragging;
    dragSourceIndex = sourceIndex;
    dragTargetIndex = sourceIndex;

    auto* src = items[(size_t)sourceIndex].get();
    dragGhostImage = src->createComponentSnapshot(src->getLocalBounds());

    const auto containerPt = itemContainer.getLocalPoint(src, e.getPosition());
    dragGhostBounds = src->getBounds().withY(containerPt.getY() - src->getHeight() / 2);

    repaint();
}

void RhythmSidebar::onItemDragMove(int sourceIndex, const juce::MouseEvent& e)
{
    if (dragPhase != DragPhase::Dragging) return;

    auto* src = (sourceIndex >= 0 && sourceIndex < (int)items.size())
                ? items[(size_t)sourceIndex].get() : nullptr;
    if (!src) return;

    const auto containerPt = itemContainer.getLocalPoint(src, e.getPosition());
    dragGhostBounds = dragGhostBounds.withY(containerPt.getY() - src->getHeight() / 2);
    dragTargetIndex = computeTargetIndex(containerPt.getY());
    repaint();
}

void RhythmSidebar::onItemDragEnd(int sourceIndex, const juce::MouseEvent&)
{
    if (dragPhase != DragPhase::Dragging) { cancelDrag(); return; }

    if (dragTargetIndex >= 0
        && dragTargetIndex < (int)items.size()
        && dragTargetIndex != sourceIndex)
    {
        commitDrag();
    }
    else
    {
        cancelDrag();
    }
}

int RhythmSidebar::computeTargetIndex(int containerLocalY) const
{
    if (items.empty()) return 0;
    return juce::jlimit(0, (int)items.size() - 1, containerLocalY / kItemH);
}

void RhythmSidebar::commitDrag()
{
    const int src = dragSourceIndex;
    const int dst = dragTargetIndex;
    const int prevSelected = selectedIndex;

    int newSelected = prevSelected;

    if (src < dst)
    {
        for (int i = src; i < dst; ++i)
        {
            proc.swapRhythms(i, i + 1);
            if (newSelected == i)          newSelected = i + 1;
            else if (newSelected == i + 1) newSelected = i;
        }
    }
    else if (src > dst)
    {
        for (int i = src; i > dst; --i)
        {
            proc.swapRhythms(i - 1, i);
            if (newSelected == i)          newSelected = i - 1;
            else if (newSelected == i - 1) newSelected = i;
        }
    }

    dragPhase = DragPhase::Idle;
    dragSourceIndex = -1;
    dragTargetIndex = -1;
    dragGhostImage = juce::Image();

    refreshItems();
    setSelectedIndex(newSelected);

    if (onRhythmsReordered) onRhythmsReordered(newSelected);
    repaint();
}

void RhythmSidebar::cancelDrag()
{
    dragPhase = DragPhase::Idle;
    dragSourceIndex = -1;
    dragTargetIndex = -1;
    dragGhostImage = juce::Image();
    repaint();
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

void RhythmSidebar::timerCallback()
{
    // Re-sync if rhythm count changed after construction (e.g. host calls
    // setStateInformation() after the editor is already open).
    if (proc.getNumRhythms() != (int)items.size())
        refreshItems();

    // Refresh mini-circle patterns and colour after a hot-swap. The Rhythm at
    // each slot was overwritten in place by handleAsyncUpdate but SidebarItem
    // caches the patterns from setRhythm() — so the mini-circle would otherwise
    // keep drawing the previous rhythm's pattern.
    const int currentEpoch = proc.rhythmSwapEpoch.load(std::memory_order_acquire);
    if (currentEpoch != lastSwapEpoch)
    {
        lastSwapEpoch = currentEpoch;
        for (int i = 0; i < (int)items.size() && i < proc.getNumRhythms(); ++i)
        {
            const Rhythm& r = proc.getRhythm(i);
            const juce::Colour col = MuClidLookAndFeel::rhythmPalette[r.colourIndex % 30];
            items[i]->setRhythm(&r, col);
        }
    }

    for (int i = 0; i < (int)items.size(); ++i)
        items[i]->setPendingSwap(proc.hasPendingSwap(i));
}
