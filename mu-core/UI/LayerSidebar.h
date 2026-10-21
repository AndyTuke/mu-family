#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Plugin/ProcessorBase.h"
#include "UI/SidebarItem.h"
#include "UI/Components/AddButton.h"
#include "UI/Components/MuLookAndFeel.h"
#include "UI/Components/StepIndicator.h"
#include "UI/Components/SegmentControl.h"
#include "Control/LiveView.h"

// Shared left sidebar for every mu-product. Renders one SidebarItem per
// "channel" (layer) from ProcessorBase's channel metadata, an Add button at the
// bottom, selection, and drag-to-reorder. A host with more layers than fit (mu_limits::kLayersPerPage) shows
// them a page at a time, the page switch taking the Add slot. Identical UX family-wide — only the
// per-layer mini-graphic (and its animation) is product-specific, injected via
// the createMiniVisual hook. Reorder and the pending-swap badge come from the processor's
// layer API (canReorderLayers / swapLayers / hasPendingSwap / cancelPendingSwap).
class LayerSidebar : public juce::Component, private juce::Timer, private mu_core::LiveView::Listener
{
public:
    static constexpr int kWidth = 82;

    LayerSidebar(ProcessorBase& processor, const juce::String& addButtonLabel);
    ~LayerSidebar() override { setLiveView(nullptr); stopTimer(); }

    // Performance mode: while the live state is in the mode, the selection follows its selected layer
    // (kept inside the layer count) and the Add slot becomes a read-only "Layer n/N" indicator.
    // Outside the mode the selection stays local. nullptr (the default) never follows.
    void setLiveView(const mu_core::LiveView* view);
    // The read-only indicator, so the shell can let the pointer reach it through its input shield.
    juce::Component& getLayerIndicator() noexcept { return layerIndicator; }
    bool isFollowingLiveState() const noexcept { return live != nullptr && live->isPerformanceMode(); }

    void refreshItems();
    void repaintItems();
    // Selects layer `i`. The shown page follows the selection: a layer on another page flips to it.
    void setSelectedIndex(int i);
    int  getSelectedIndex() const noexcept { return selectedIndex; }

    // ── Pages (a host with more layers than mu_limits::kLayersPerPage) ───────────────────────────────
    // The pages are derived from the layer count; one page (every product but mu-Tronic) shows no switch.
    int  getNumPages() const noexcept;
    int  getShownPage() const noexcept { return shownPage; }
    static int pageOf(int layer) noexcept { return layer / mu_limits::kLayersPerPage; }
    // Shows `page` (clamped) and selects the layer at the same position on it, so the panel never shows nothing.
    void showPage(int page);
    // PageUp / PageDown: one page further or back, no wrap. False when there is nowhere to go.
    bool showAdjacentPage(int delta);
    // The page switch, so the shell can let the pointer reach it through its input shield.
    juce::Component& getPageSwitch() noexcept { return pageSwitch; }

    // Drops layer `source` on `target` exactly as a finished drag does (the host's reorder, then the selection and the
    // onLayersReordered callback). Exposed so a test can drive a drag without a mouse.
    void dropLayerForTest(int source, int target) { dragSourceIndex = source; dragTargetIndex = target; dragPhase = DragPhase::Dragging; commitDrag(); }

    // Flash a layer's hit-pulse ring — product drives this from its play state.
    void pulseItem(int idx);

    // Metal style (mu-Clid): the sidebar is a metal panel with a painted border in the app
    // colour, and each layer sits in its own raised box. Off keeps the flat family look.
    void setMetalStyle(bool m);
    void lookAndFeelChanged() override { setMetalStyle(MuLookAndFeel::isMetal(*this)); }

    // ── Product hooks ─────────────────────────────────────────────────────────
    // Builds layer `index`'s mini-graphic; the visual calls `pulse` on its hit edge.
    std::function<std::unique_ptr<juce::Component>(int index, std::function<void()> pulse)> createMiniVisual; // required
    std::function<void()> onAnimationTick;   // optional, 5 Hz; poll + pulseItem()

    // ── User actions ──────────────────────────────────────────────────────────
    std::function<void(int)> onLayerSelected;   // also fired when a page change moves the selection
    std::function<void()>    onAddLayer;
    std::function<void(int)> onLayersReordered;

    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;

private:
    ProcessorBase& proc;
    juce::String   noun;
    void updateAddEnabled();   // Add is greyed (not hidden) at kMaxLayers, so the sidebar never reflows

    // Pages. The items of every page exist; only those of the shown page are visible and laid out, by their
    // position within the page.
    int  shownPage = 0;
    juce::Rectangle<int> pageCaptionR;   // the engraved "Page" caption over the page switch
    SegmentControl pageSwitch { {}, SegmentControl::ActiveStyle::General, SegmentControl::DrawStyle::Lcd };
    void syncPageSwitch();     // labels and selection from the count and the shown page
    void applyPage();          // visibility + layout of the shown page
    int  firstOnPage() const noexcept { return shownPage * mu_limits::kLayersPerPage; }
    int  visibleCount() const noexcept;
    bool showsPageSwitch() const noexcept { return getNumPages() > 1; }   // also in Performance mode, read-only behind the shield

    // Holds the items; in metal style it paints their raised boxes behind them, so each
    // box's shadow falls across the gap below it rather than being clipped by its item.
    struct ItemContainer : juce::Component
    {
        std::function<void(juce::Graphics&)> onPaint;
        void paint(juce::Graphics& g) override { if (onPaint) onPaint(g); }
    };

    juce::Viewport  viewport;
    ItemContainer   itemContainer;
    bool            metal = false;
    std::vector<std::unique_ptr<SidebarItem>> items;
    AddButton addButton;
    StepIndicator layerIndicator;               // replaces the Add slot in Performance mode
    const mu_core::LiveView* live = nullptr;
    bool shownFollowing = false;                // the Add slot currently shows the indicator
    void liveStateChanged() override { followLiveState(); }
    void followLiveState();                     // selection + indicator from the live state
    juce::ComponentAnimator animator;

    int selectedIndex = 0;

    static constexpr int kItemH   = 80;
    // Item layout from resized(): the gap above, between and below the items (equal, so a
    // full sidebar is evenly distributed top to bottom; capped so a few items stay near the
    // top), and the side inset. Every item position / drop index goes through itemBounds.
    int itemGap = 0, itemInsetX = 0;
    juce::Rectangle<int> itemBounds(int i) const;
    static constexpr int kAddBtnH = 34;   // the Add button's slot; the page switch takes the same slot

    enum class DragPhase { Idle, Dragging };
    DragPhase            dragPhase = DragPhase::Idle;
    int                  dragSourceIndex = -1;
    int                  dragTargetIndex = -1;
    juce::Image          dragGhostImage;
    juce::Rectangle<int> dragGhostBounds;

    juce::Colour colourFor(int i) const;
    void layoutItems(bool animate, int newItemIndex = -1);
    void onItemDragStart(int sourceIndex, const juce::MouseEvent& e);
    void onItemDragMove (int sourceIndex, const juce::MouseEvent& e);
    void onItemDragEnd  (int sourceIndex, const juce::MouseEvent& e);
    int  computeTargetIndex(int containerLocalY) const;
    void commitDrag();
    void cancelDrag();
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LayerSidebar)
};
