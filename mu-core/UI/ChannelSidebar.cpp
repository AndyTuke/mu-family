#include "ChannelSidebar.h"

ChannelSidebar::ChannelSidebar(ProcessorBase& processor, const juce::String& addButtonLabel)
    : proc(processor), addButton(addButtonLabel)
{
    viewport.setViewedComponent(&itemContainer, false);
    viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport);
    addAndMakeVisible(addButton);

    addButton.onClick = [this] { if (onAddChannel) onAddChannel(); };
    startTimerHz(5);   // re-sync count + poll pending-swap + product animation tick
}

juce::Colour ChannelSidebar::colourFor(int i) const
{
    const int idx = proc.getChannelColourIndex(i);
    return MuLookAndFeel::channelPalette[(size_t) (idx % MuLookAndFeel::kChannelPaletteSize)];
}

void ChannelSidebar::refreshItems()
{
    const int prevCount = (int) items.size();

    for (auto& item : items)
        itemContainer.removeChildComponent(item.get());
    items.clear();

    const int n = proc.getNumChannels();
    for (int i = 0; i < n; ++i)
    {
        auto item = std::make_unique<SidebarItem>(i);
        item->setName(proc.getChannelName(i));
        item->setColour(colourFor(i));
        item->setSelected(i == selectedIndex);
        if (createMiniVisual) item->setMiniVisual(createMiniVisual(i));

        item->onSelected = [this](int idx)
        {
            setSelectedIndex(idx);
            if (onChannelSelected) onChannelSelected(idx);
        };
        item->onDragStart = [this](int idx, const juce::MouseEvent& e) { onItemDragStart(idx, e); };
        item->onDragMove  = [this](int idx, const juce::MouseEvent& e) { onItemDragMove (idx, e); };
        item->onDragEnd   = [this](int idx, const juce::MouseEvent& e) { onItemDragEnd  (idx, e); };
        item->onCancelPendingSwap = [this](int idx) { if (onCancelPendingSwap) onCancelPendingSwap(idx); };

        itemContainer.addAndMakeVisible(item.get());
        items.push_back(std::move(item));
    }

    itemContainer.setSize(kWidth, (int) items.size() * kItemH);
    resized();

    // Animate a freshly-added item sliding in (skip on initial construction).
    if (n > prevCount && prevCount > 0)
        layoutItems(true, n - 1);
}

void ChannelSidebar::layoutItems(bool animate, int newItemIndex)
{
    using mu_ui::s;
    const int w  = itemContainer.getWidth();
    const int ih = s(kItemH);
    for (int i = 0; i < (int) items.size(); ++i)
    {
        const juce::Rectangle<int> target(0, i * ih, w, ih);
        if (animate && i == newItemIndex)
        {
            items[(size_t) i]->setAlpha(0.0f);
            items[(size_t) i]->setBounds(0, (i + 1) * ih, w, ih);
            animator.animateComponent(items[(size_t) i].get(), target, 1.0f, 120, false, 0.0, 1.0);
        }
        else if (animate)
        {
            animator.animateComponent(items[(size_t) i].get(), target, 1.0f, 80, false, 1.0, 1.0);
        }
        else
        {
            items[(size_t) i]->setBounds(target);
        }
    }
}

void ChannelSidebar::repaintItems()
{
    for (auto& item : items) item->repaint();
    itemContainer.repaint();
    repaint();
}

void ChannelSidebar::setSelectedIndex(int i)
{
    selectedIndex = i;
    for (int j = 0; j < (int) items.size(); ++j)
        items[(size_t) j]->setSelected(j == i);
}

void ChannelSidebar::pulseItem(int idx)
{
    if (idx >= 0 && idx < (int) items.size())
        items[(size_t) idx]->pulse();
}

void ChannelSidebar::paint(juce::Graphics& g)
{
    using Id = MuLookAndFeel::ColourIds;
    g.setColour(MuLookAndFeel::colour(Id::sidebarBackground));
    g.fillAll();
    g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawLine((float) (getWidth() - 1), 0.0f, (float) (getWidth() - 1), (float) getHeight(), 1.0f);
}

void ChannelSidebar::paintOverChildren(juce::Graphics& g)
{
    if (dragPhase != DragPhase::Dragging) return;

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

void ChannelSidebar::onItemDragStart(int sourceIndex, const juce::MouseEvent& e)
{
    if (! onSwapChannels) return;   // reordering disabled for this product
    if (sourceIndex < 0 || sourceIndex >= (int) items.size()) return;

    dragPhase = DragPhase::Dragging;
    dragSourceIndex = sourceIndex;
    dragTargetIndex = sourceIndex;

    auto* src = items[(size_t) sourceIndex].get();
    dragGhostImage = src->createComponentSnapshot(src->getLocalBounds());

    const auto containerPt = itemContainer.getLocalPoint(src, e.getPosition());
    dragGhostBounds = src->getBounds().withY(containerPt.getY() - src->getHeight() / 2);
    repaint();
}

void ChannelSidebar::onItemDragMove(int sourceIndex, const juce::MouseEvent& e)
{
    if (dragPhase != DragPhase::Dragging) return;
    auto* src = (sourceIndex >= 0 && sourceIndex < (int) items.size())
                ? items[(size_t) sourceIndex].get() : nullptr;
    if (! src) return;

    const auto containerPt = itemContainer.getLocalPoint(src, e.getPosition());
    dragGhostBounds = dragGhostBounds.withY(containerPt.getY() - src->getHeight() / 2);
    dragTargetIndex = computeTargetIndex(containerPt.getY());
    repaint();
}

void ChannelSidebar::onItemDragEnd(int sourceIndex, const juce::MouseEvent&)
{
    if (dragPhase != DragPhase::Dragging) { cancelDrag(); return; }

    if (dragTargetIndex >= 0 && dragTargetIndex < (int) items.size() && dragTargetIndex != sourceIndex)
        commitDrag();
    else
        cancelDrag();
}

int ChannelSidebar::computeTargetIndex(int containerLocalY) const
{
    if (items.empty()) return 0;
    return juce::jlimit(0, (int) items.size() - 1, containerLocalY / kItemH);
}

void ChannelSidebar::commitDrag()
{
    const int src = dragSourceIndex;
    const int dst = dragTargetIndex;
    int newSelected = selectedIndex;

    if (src < dst)
    {
        for (int i = src; i < dst; ++i)
        {
            if (onSwapChannels) onSwapChannels(i, i + 1);
            if (newSelected == i)          newSelected = i + 1;
            else if (newSelected == i + 1) newSelected = i;
        }
    }
    else if (src > dst)
    {
        for (int i = src; i > dst; --i)
        {
            if (onSwapChannels) onSwapChannels(i - 1, i);
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
    if (onChannelsReordered) onChannelsReordered(newSelected);
    repaint();
}

void ChannelSidebar::cancelDrag()
{
    dragPhase = DragPhase::Idle;
    dragSourceIndex = -1;
    dragTargetIndex = -1;
    dragGhostImage = juce::Image();
    repaint();
}

void ChannelSidebar::resized()
{
    using mu_ui::s;
    const int w  = getWidth();
    const int h  = getHeight();
    const int ih = s(kItemH);
    const int ah = s(kAddBtnH);
    const int addBtnY = h - ah - s(4);

    addButton.setBounds(s(4), addBtnY, w - s(8), ah);
    viewport.setBounds(0, 0, w, addBtnY - s(2));
    itemContainer.setSize(w, juce::jmax(1, (int) items.size() * ih));

    for (int i = 0; i < (int) items.size(); ++i)
        items[(size_t) i]->setBounds(0, i * ih, w, ih);
}

void ChannelSidebar::timerCallback()
{
    // Re-sync if the channel count changed outside the editor (e.g. host
    // setStateInformation after the editor opened).
    if (proc.getNumChannels() != (int) items.size())
        refreshItems();

    if (isPendingSwap)
        for (int i = 0; i < (int) items.size(); ++i)
            items[(size_t) i]->setPendingSwap(isPendingSwap(i));

    if (onAnimationTick) onAnimationTick();
}
