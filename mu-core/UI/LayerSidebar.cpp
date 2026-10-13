#include "LayerSidebar.h"
#include "UI/PerformanceIndication.h"

LayerSidebar::LayerSidebar(ProcessorBase& processor, const juce::String& addButtonLabel)
    : proc(processor), noun(addButtonLabel), addButton(addButtonLabel)
{
    viewport.setViewedComponent(&itemContainer, false);
    viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport);
    addAndMakeVisible(addButton);
    addChildComponent(layerIndicator);

    addButton.onClick = [this] { if (onAddLayer) onAddLayer(); };

    // Metal style: every visible item's raised box, all shadows first, then the faces.
    itemContainer.onPaint = [this](juce::Graphics& g)
    {
        if (! metal) return;
        const auto appCol = MuLookAndFeel::appAccent(*this);
        for (auto& item : items)
            if (item->isVisible())
                MuLookAndFeel::drawRaisedSubPanelShadow(g, SidebarItem::metalBox(item->getBounds()));
        for (auto& item : items)
            if (item->isVisible())
                MuLookAndFeel::drawRaisedSubPanel(g, SidebarItem::metalBox(item->getBounds()), appCol);
        if (MuLookAndFeel::hasScrews(*this))
            for (auto& item : items)
                if (item->isVisible())
                    MuLookAndFeel::drawSubPanelScrews(g, SidebarItem::metalBox(item->getBounds()));
    };
    startTimerHz(5);   // re-sync count + poll pending-swap + product animation tick
}

juce::Colour LayerSidebar::colourFor(int i) const
{
    const int idx = proc.getLayerColourIndex(i);
    return MuLookAndFeel::layerPalette[(size_t) (idx % MuLookAndFeel::kLayerPaletteSize)];
}

void LayerSidebar::refreshItems()
{
    const int prevCount = (int) items.size();

    for (auto& item : items)
        itemContainer.removeChildComponent(item.get());
    items.clear();

    const int n = proc.getNumLayers();
    for (int i = 0; i < n; ++i)
    {
        auto item = std::make_unique<SidebarItem>(i);
        item->setName(proc.getLayerName(i));
        item->setColour(colourFor(i));
        item->setSelected(i == selectedIndex);
        item->setMetalStyle(metal);
        if (createMiniVisual) item->setMiniVisual(createMiniVisual(i, [this, i] { pulseItem(i); }));

        item->onSelected = [this](int idx)
        {
            setSelectedIndex(idx);
            if (onLayerSelected) onLayerSelected(idx);
        };
        item->onDragStart = [this](int idx, const juce::MouseEvent& e) { onItemDragStart(idx, e); };
        item->onDragMove  = [this](int idx, const juce::MouseEvent& e) { onItemDragMove (idx, e); };
        item->onDragEnd   = [this](int idx, const juce::MouseEvent& e) { onItemDragEnd  (idx, e); };
        item->onCancelPendingSwap = [this](int idx) { proc.cancelPendingSwap(idx); };

        itemContainer.addAndMakeVisible(item.get());
        items.push_back(std::move(item));
    }

    updateAddEnabled();

    itemContainer.setSize(kWidth, (int) items.size() * kItemH);
    resized();

    // Animate a freshly-added item sliding in (skip on initial construction).
    if (n > prevCount && prevCount > 0)
        layoutItems(true, n - 1);
}

void LayerSidebar::setLiveView(const mu_core::LiveView* view)
{
    if (live == view) return;
    if (live != nullptr) live->removeListener(this);
    live = view;
    if (live != nullptr) live->addListener(this);
    followLiveState();
}

// In Performance mode: select the live layer (the editor shows it through onLayerSelected) and keep
// the "Layer n/N" text current; on a change of mode swap the Add slot.
void LayerSidebar::followLiveState()
{
    const bool on = isFollowingLiveState();
    if (on != shownFollowing)
    {
        shownFollowing = on;
        resized();
    }
    if (! on) return;

    const int layers = proc.getNumLayers();
    const int layer  = mu_ui::perf::followedLayer(live->getSelectedLayer(), layers);
    if (layer != selectedIndex)
    {
        setSelectedIndex(layer);
        if (onLayerSelected) onLayerSelected(layer);
    }
    layerIndicator.setText(mu_ui::perf::layerIndicatorText(layer, layers), {}, false);
}

void LayerSidebar::updateAddEnabled()
{
    const bool room = proc.getNumLayers() < mu_limits::kMaxLayers;
    if (addButton.isEnabled() == room) return;
    addButton.setEnabled(room);
    addButton.setTooltip(room ? juce::String() : "Maximum of " + juce::String(mu_limits::kMaxLayers) + " " + noun + "s");
}

void LayerSidebar::layoutItems(bool animate, int newItemIndex)
{
    using mu_ui::s;
    for (int i = 0; i < (int) items.size(); ++i)
    {
        const auto target = itemBounds(i);
        if (animate && i == newItemIndex)
        {
            items[(size_t) i]->setAlpha(0.0f);
            items[(size_t) i]->setBounds(itemBounds(i + 1));
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

void LayerSidebar::repaintItems()
{
    for (auto& item : items) item->repaint();
    itemContainer.repaint();
    repaint();
}

void LayerSidebar::setSelectedIndex(int i)
{
    selectedIndex = i;
    for (int j = 0; j < (int) items.size(); ++j)
        items[(size_t) j]->setSelected(j == i);
}

void LayerSidebar::pulseItem(int idx)
{
    if (idx >= 0 && idx < (int) items.size())
        items[(size_t) idx]->pulse();
}

void LayerSidebar::setMetalStyle(bool m)
{
    metal = m;
    for (auto& item : items) item->setMetalStyle(m);
    resized();
    repaint();
    itemContainer.repaint();
}

void LayerSidebar::paint(juce::Graphics& g)
{
    using Id = MuLookAndFeel::ColourIds;
    g.setColour(MuLookAndFeel::colour(Id::sidebarBackground));
    g.fillAll();
    if (metal)
    {
        MuLookAndFeel::drawAccentPanel(g, getLocalBounds().reduced(2).toFloat(),
                                       MuLookAndFeel::appAccent(*this));
        return;
    }
    g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawLine((float) (getWidth() - 1), 0.0f, (float) (getWidth() - 1), (float) getHeight(), 1.0f);
}

void LayerSidebar::paintOverChildren(juce::Graphics& g)
{
    // Panel screws over the items, set into the painted border.
    if (metal && MuLookAndFeel::hasScrews(*this))
        MuLookAndFeel::drawPanelScrews(g, getLocalBounds().reduced(2).toFloat());

    if (dragPhase != DragPhase::Dragging) return;

    const int viewX = viewport.getX();
    const int viewY = viewport.getY();
    const int scrollY = viewport.getViewPositionY();

    if (dragTargetIndex >= 0)
    {
        const int lineY = viewY + itemBounds(dragTargetIndex).getY() - itemGap / 2 - scrollY;
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

void LayerSidebar::onItemDragStart(int sourceIndex, const juce::MouseEvent& e)
{
    if (! proc.canReorderLayers()) return;   // reordering disabled for this product
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

void LayerSidebar::onItemDragMove(int sourceIndex, const juce::MouseEvent& e)
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

void LayerSidebar::onItemDragEnd(int sourceIndex, const juce::MouseEvent&)
{
    if (dragPhase != DragPhase::Dragging) { cancelDrag(); return; }

    if (dragTargetIndex >= 0 && dragTargetIndex < (int) items.size() && dragTargetIndex != sourceIndex)
        commitDrag();
    else
        cancelDrag();
}

int LayerSidebar::computeTargetIndex(int containerLocalY) const
{
    if (items.empty()) return 0;
    const int pitch = itemBounds(1).getY() - itemBounds(0).getY();
    return juce::jlimit(0, (int) items.size() - 1, (containerLocalY - itemGap / 2) / juce::jmax(1, pitch));
}

void LayerSidebar::commitDrag()
{
    const int src = dragSourceIndex;
    const int dst = dragTargetIndex;
    int newSelected = selectedIndex;

    if (src < dst)
    {
        for (int i = src; i < dst; ++i)
        {
            proc.swapLayers(i, i + 1);
            if (newSelected == i)          newSelected = i + 1;
            else if (newSelected == i + 1) newSelected = i;
        }
    }
    else if (src > dst)
    {
        for (int i = src; i > dst; --i)
        {
            proc.swapLayers(i - 1, i);
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
    if (onLayersReordered) onLayersReordered(newSelected);
    repaint();
}

void LayerSidebar::cancelDrag()
{
    dragPhase = DragPhase::Idle;
    dragSourceIndex = -1;
    dragTargetIndex = -1;
    dragGhostImage = juce::Image();
    repaint();
}

void LayerSidebar::resized()
{
    using mu_ui::s;
    const int w  = getWidth();
    const int h  = getHeight();
    const int ih = s(kItemH);
    const int ah = s(kAddBtnH);

    // Hide the Add button (and expand the viewport to fill) when the product
    // hasn't wired onAddLayer — avoids a visible, interactive control that
    // does nothing (e.g. mu-toni with no engine yet defined).
    // In Performance mode the slot holds the read-only layer indicator instead, even with no Add.
    const bool following = shownFollowing;
    const bool showAdd   = static_cast<bool>(onAddLayer) || following;
    addButton.setVisible(static_cast<bool>(onAddLayer) && ! following);
    layerIndicator.setVisible(following);

    // Metal style keeps the items and the Add button clear of the painted border, and
    // with screws, clear of the corner screws above the first item / below the Add button.
    const bool screwed = metal && MuLookAndFeel::hasScrews(*this);
    using LF = MuLookAndFeel;
    const int top   = screwed ? s(LF::kScrewedPanelInset - 3)   /* metalBox insets 3 */ : metal ? s(LF::kSidebarMetalTop) : 0;
    const int edgeX = s(screwed ? LF::kSidebarScrewedEdgeX : metal ? LF::kSidebarMetalEdge : LF::kSidebarFlatEdge);
    const int itemX = screwed ? s(LF::kSidebarScrewedItemX) : 0;   // with screws the layer boxes are a little narrower too
    const int edgeY = s(screwed ? LF::kScrewedPanelInset : metal ? LF::kSidebarMetalEdge : LF::kSidebarFlatEdge);
    if (showAdd)
    {
        const int addBtnY = h - ah - edgeY;
        addButton.setBounds(edgeX, addBtnY, w - 2 * edgeX, ah);
        layerIndicator.setBounds(addButton.getBounds());
        viewport.setBounds(0, top, w, addBtnY - s(2) - top);
    }
    else
    {
        viewport.setBounds(0, top, w, h - 2 * top);
    }

    // Spread the items evenly: equal gaps above, between and below them, capped so a few
    // items stay grouped near the top. If they don't fit, no gap — the viewport scrolls.
    const int n     = (int) items.size();
    const int spare = viewport.getHeight() - n * ih;
    itemGap    = spare > 0 ? juce::jmin(spare / (n + 1), s(kItemH / 4)) : 0;
    itemInsetX = itemX;
    itemContainer.setSize(w, juce::jmax(1, n * (ih + itemGap) + itemGap));

    for (int i = 0; i < n; ++i)
        items[(size_t) i]->setBounds(itemBounds(i));
}

juce::Rectangle<int> LayerSidebar::itemBounds(int i) const
{
    using mu_ui::s;
    const int ih = s(kItemH);
    return { itemInsetX, itemGap + i * (ih + itemGap), itemContainer.getWidth() - 2 * itemInsetX, ih };
}

void LayerSidebar::timerCallback()
{
    // Re-sync if the channel count changed outside the editor (e.g. host
    // setStateInformation after the editor opened).
    if (proc.getNumLayers() != (int) items.size())
        refreshItems();

    updateAddEnabled();   // the count can change outside the editor (host state restore)

    // Sync Add button visibility in case onAddLayer was wired (or cleared)
    // after the last resized() call.
    followLiveState();
    if (addButton.isVisible() != (static_cast<bool>(onAddLayer) && ! shownFollowing))
        resized();

    // Show the staging badge on every layer whose preset swap is waiting for its boundary.
    for (int i = 0; i < (int) items.size(); ++i)
        items[(size_t) i]->setPendingSwap(proc.hasPendingSwap(i));

    if (onAnimationTick) onAnimationTick();
}
