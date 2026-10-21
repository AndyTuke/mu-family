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
    addChildComponent(pageSwitch);
    addChildComponent(sceneGrid);
    sceneGrid.bind(proc.getLiveView(), proc.getControlSink());

    addButton.onClick = [this] { if (onAddLayer) onAddLayer(); };
    pageSwitch.onChange = [this](int page) { showPage(page); };

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
    const bool slots = proc.layersAreSlots();
    for (int i = 0; i < n; ++i)
    {
        auto item = std::make_unique<SidebarItem>(i);
        item->setName(proc.getLayerName(i));
        item->setColour(colourFor(i));
        item->setSelected(i == selectedIndex);
        item->setMetalStyle(metal);
        // A host of fixed slots numbers them and shows an empty one as a "+": no mini-graphic is built for it.
        const bool emptySlot = slots && proc.getLayer(i) != nullptr && proc.getLayer(i)->typeId() == 0;
        if (slots) { item->setSlotNumber(i + 1); item->setEmpty(emptySlot); }
        if (createMiniVisual && ! emptySlot) item->setMiniVisual(createMiniVisual(i, [this, i] { pulseItem(i); }));

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
    shownPage = juce::jlimit(0, juce::jmax(0, getNumPages() - 1), shownPage);
    syncPageSwitch();
    applyPage();
    itemContainer.setSize(kWidth, visibleCount() * kItemH);
    resized();

    // Animate a freshly-added item sliding in (skip on initial construction).
    if (n > prevCount && prevCount > 0 && getNumPages() == 1)
        layoutItems(true, n - 1);
}

// ── Pages ────────────────────────────────────────────────────────────────────

int LayerSidebar::getNumPages() const noexcept
{
    const int n = proc.getNumLayers();
    return juce::jmax(1, (n + mu_limits::kLayersPerPage - 1) / mu_limits::kLayersPerPage);
}

// The items of the shown page: a full page except the last, which holds what is left.
int LayerSidebar::visibleCount() const noexcept
{
    return juce::jlimit(0, mu_limits::kLayersPerPage, (int) items.size() - firstOnPage());
}

// The page numbers, "1" and "2": a range does not fit the 30 px segment, so the slot's caption says "Page".
void LayerSidebar::syncPageSwitch()
{
    const int pages = getNumPages();
    if (pages <= 1) { pageSwitch.setVisible(false); return; }
    std::vector<juce::String> labels;
    for (int p = 0; p < pages; ++p) labels.push_back(juce::String(p + 1));
    pageSwitch.setLabels(labels);
    pageSwitch.setSelectedIndex(shownPage);
    pageSwitch.setVisible(showsPageSwitch());
}

void LayerSidebar::applyPage()
{
    for (int i = 0; i < (int) items.size(); ++i)
    {
        const bool onShown = pageOf(i) == shownPage;
        items[(size_t) i]->setVisible(onShown);
        if (onShown) items[(size_t) i]->setBounds(itemBounds(i));
    }
    itemContainer.repaint();
}

void LayerSidebar::showPage(int page)
{
    page = juce::jlimit(0, getNumPages() - 1, page);
    if (page == shownPage) { syncPageSwitch(); return; }

    // The layer at the same position on the new page becomes the selection, so the main panel always shows a layer.
    const int perPage = mu_limits::kLayersPerPage;
    const int target = juce::jlimit(0, juce::jmax(0, (int) items.size() - 1), page * perPage + selectedIndex % perPage);
    shownPage = page;
    syncPageSwitch();
    applyPage();
    resized();
    setSelectedIndex(target);
    if (onLayerSelected) onLayerSelected(target);
}

bool LayerSidebar::showAdjacentPage(int delta)
{
    const int page = shownPage + (delta < 0 ? -1 : 1);
    if (page < 0 || page >= getNumPages()) return false;
    showPage(page);
    return true;
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
    // Names are cached on the tiles, so a rename must be pulled in here.
    for (size_t i = 0; i < items.size(); ++i) items[i]->setName(proc.getLayerName((int) i));
    for (auto& item : items) item->repaint();
    itemContainer.repaint();
    repaint();
}

void LayerSidebar::setSelectedIndex(int i)
{
    selectedIndex = i;
    for (int j = 0; j < (int) items.size(); ++j)
        items[(size_t) j]->setSelected(j == i);
    // The page follows the selection (a layer picked by the surface or the mixer may be on the other page).
    if (i >= 0 && i < (int) items.size() && pageOf(i) != shownPage)
    {
        shownPage = pageOf(i);
        syncPageSwitch();
        applyPage();
        resized();
    }
}

void LayerSidebar::pulseItem(int idx)
{
    if (idx >= 0 && idx < (int) items.size())
        items[(size_t) idx]->pulse();
}

void LayerSidebar::setShowScenes(bool show)
{
    if (showScenes == show) return;
    showScenes = show;
    sceneGrid.setVisible(show);
    resized();
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
    if (pageSwitch.isVisible() && ! pageCaptionR.isEmpty())
        MuLookAndFeel::drawEngravedText(g, "Page", pageCaptionR, juce::Justification::centred,
                                        MuLookAndFeel::colour(MuLookAndFeel::ColourIds::labelText));

    // Panel screws over the items, set into the painted border.
    if (metal && MuLookAndFeel::hasScrews(*this))
        MuLookAndFeel::drawPanelScrews(g, getLocalBounds().reduced(2).toFloat());

    if (dragPhase != DragPhase::Dragging) return;

    const int viewX = viewport.getX();
    const int viewY = viewport.getY();
    const int scrollY = viewport.getViewPositionY();

    if (dragTargetIndex >= 0 && proc.layersAreSlots())
    {
        // A slot host exchanges the two slots, so the target is outlined (nothing in between moves) instead of marked by an insertion line.
        if (dragTargetIndex != dragSourceIndex)
        {
            const auto r = itemBounds(dragTargetIndex).translated(viewX, viewY - scrollY).toFloat().reduced(1.0f);
            g.setColour(MuLookAndFeel::appAccent(*this));
            g.drawRoundedRectangle(r, 4.0f, 2.0f);
        }
    }
    else if (dragTargetIndex >= 0)
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
    // A drag stays within the shown page.
    return firstOnPage() + juce::jlimit(0, juce::jmax(0, visibleCount() - 1), (containerLocalY - itemGap / 2) / juce::jmax(1, pitch));
}

void LayerSidebar::commitDrag()
{
    const int src = dragSourceIndex;
    const int dst = dragTargetIndex;
    int newSelected = selectedIndex;

    if (proc.layersAreSlots())
    {
        // Fixed slots: one exchange of exactly two slots (an Empty target takes the layer), all or nothing. A refused
        // swap (the audio thread did not quiesce) leaves every slot and the selection as they were.
        if (proc.swapLayers(src, dst))
        {
            if (newSelected == src)      newSelected = dst;
            else if (newSelected == dst) newSelected = src;
        }
    }
    else if (src < dst)
    {
        // A list: the layer moves by a chain of adjacent swaps, so the layers in between shift. The chain stops at the first refusal.
        for (int i = src; i < dst; ++i)
        {
            if (! proc.swapLayers(i, i + 1)) break;
            if (newSelected == i)          newSelected = i + 1;
            else if (newSelected == i + 1) newSelected = i;
        }
    }
    else if (src > dst)
    {
        for (int i = src; i > dst; --i)
        {
            if (! proc.swapLayers(i - 1, i)) break;
            if (newSelected == i)          newSelected = i - 1;
            else if (newSelected == i - 1) newSelected = i;
        }
    }

    dragPhase = DragPhase::Idle;
    dragSourceIndex = -1;
    dragTargetIndex = -1;
    dragGhostImage = juce::Image();

    // Also after a refusal: the host's onLayerLeaving may have released the editor's bindings, and the callback rebinds the shown layer.
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
    const bool paged     = showsPageSwitch();   // the page switch takes the Add slot (a paged host has no Add button)
    const bool showAdd   = static_cast<bool>(onAddLayer) || following || paged;
    addButton.setVisible(static_cast<bool>(onAddLayer) && ! following && ! paged);
    layerIndicator.setVisible(following && ! paged);   // a paged host shows its page switch, which already follows the surface
    pageSwitch.setVisible(paged);

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
        // The page switch: an engraved "Page" caption over the segments, in the same slot.
        const int capH = s(LF::kClientNameH);
        pageCaptionR = addButton.getBounds().withHeight(capH);
        pageSwitch.setBounds(addButton.getBounds().withTrimmedTop(capH));
        viewport.setBounds(0, top, w, addBtnY - s(2) - top);
    }
    else
    {
        viewport.setBounds(0, top, w, h - 2 * top);
    }

    // The scene block sits at the foot above that slot (kSpaceS clear of it) and the items give up the room it takes.
    sceneGrid.setVisible(showScenes);
    if (showScenes)
    {
        const int blockH   = SceneGrid::blockHeight();
        const int blockBot = (showAdd ? addButton.getY() : h - edgeY) - s(LF::kSpaceS);
        sceneGrid.setBounds(edgeX, blockBot - blockH, w - 2 * edgeX, blockH);
        viewport.setSize(w, juce::jmax(0, sceneGrid.getY() - s(2) - viewport.getY()));
    }

    // Spread the items evenly: equal gaps above, between and below them, capped so a few
    // items stay grouped near the top. If they don't fit, no gap — the viewport scrolls.
    const int n     = visibleCount();
    const int spare = viewport.getHeight() - n * ih;
    itemGap    = spare > 0 ? juce::jmin(spare / (n + 1), s(kItemH / 4)) : 0;
    itemInsetX = itemX;
    itemContainer.setSize(w, juce::jmax(1, n * (ih + itemGap) + itemGap));

    for (int i = 0; i < (int) items.size(); ++i)
        if (pageOf(i) == shownPage) items[(size_t) i]->setBounds(itemBounds(i));
}

// An item's place is its position within its page.
juce::Rectangle<int> LayerSidebar::itemBounds(int i) const
{
    using mu_ui::s;
    const int ih = s(kItemH);
    const int local = i % mu_limits::kLayersPerPage;
    return { itemInsetX, itemGap + local * (ih + itemGap), itemContainer.getWidth() - 2 * itemInsetX, ih };
}

void LayerSidebar::timerCallback()
{
    // Re-sync if the channel count changed outside the editor (e.g. host
    // setStateInformation after the editor opened).
    if (proc.getNumLayers() != (int) items.size())
        refreshItems();
    else if (proc.layersAreSlots())
    {
        // A slot host: a layer type changed outside the editor (a state restore, a program change).
        for (int i = 0; i < (int) items.size(); ++i)
            if (const auto* l = proc.getLayer(i); l != nullptr && (l->typeId() == 0) != items[(size_t) i]->isEmpty())
            {
                refreshItems();
                break;
            }
    }

    updateAddEnabled();   // the count can change outside the editor (host state restore)

    // Sync Add button visibility in case onAddLayer was wired (or cleared)
    // after the last resized() call.
    followLiveState();
    if (addButton.isVisible() != (static_cast<bool>(onAddLayer) && ! shownFollowing && ! showsPageSwitch()))
        resized();

    // Show the staging badge on every layer whose preset swap is waiting for its boundary.
    for (int i = 0; i < (int) items.size(); ++i)
        items[(size_t) i]->setPendingSwap(proc.hasPendingSwap(i));

    if (onAnimationTick) onAnimationTick();
}
