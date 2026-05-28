#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Plugin/ProcessorBase.h"
#include "UI/SidebarItem.h"
#include "UI/Components/AddButton.h"
#include "UI/Components/MuLookAndFeel.h"

// Shared left sidebar for every mu-product. Renders one SidebarItem per
// "channel" (layer) from ProcessorBase's channel metadata, an Add button at the
// bottom, selection, and drag-to-reorder. Identical UX family-wide — only the
// per-layer mini-graphic (and its animation) is product-specific, injected via
// the createMiniVisual hook. Reorder + pending-swap semantics are product hooks
// too, so a product without hot-swap (mu-tant) just leaves them null.
class ChannelSidebar : public juce::Component, private juce::Timer
{
public:
    static constexpr int kWidth = 82;

    ChannelSidebar(ProcessorBase& processor, const juce::String& addButtonLabel);
    ~ChannelSidebar() override { stopTimer(); }

    void refreshItems();
    void repaintItems();
    void setSelectedIndex(int i);
    int  getSelectedIndex() const noexcept { return selectedIndex; }

    // Flash a layer's hit-pulse ring — product drives this from its play state.
    void pulseItem(int idx);

    // ── Product hooks ─────────────────────────────────────────────────────────
    std::function<std::unique_ptr<juce::Component>(int)> createMiniVisual; // required
    std::function<void(int, int)> onSwapChannels;        // reorder (null = no reorder)
    std::function<bool(int)>      isPendingSwap;          // optional hot-swap badge
    std::function<void(int)>      onCancelPendingSwap;    // optional
    std::function<void()>         onAnimationTick;        // 5 Hz; poll + pulseItem()

    // ── User actions ──────────────────────────────────────────────────────────
    std::function<void(int)> onChannelSelected;
    std::function<void()>    onAddChannel;
    std::function<void(int)> onChannelsReordered;

    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;

private:
    ProcessorBase& proc;

    juce::Viewport  viewport;
    juce::Component itemContainer;
    std::vector<std::unique_ptr<SidebarItem>> items;
    AddButton addButton;
    juce::ComponentAnimator animator;

    int selectedIndex = 0;

    static constexpr int kItemH   = 80;
    static constexpr int kAddBtnH = 34;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelSidebar)
};
