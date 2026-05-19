#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "SidebarItem.h"
#include "Components/AddButton.h"
#include "Components/MuClidLookAndFeel.h"
#include "../Plugin/PluginProcessor.h"

// Left sidebar (~82px). One SidebarItem per rhythm, scrollable if needed.
// Add rhythm button at bottom. Variable ordering supported for future drag-to-reorder.
class RhythmSidebar : public juce::Component, private juce::Timer
{
public:
    static constexpr int kWidth = 82;

    explicit RhythmSidebar(PluginProcessor& p);

    void refreshItems();
    void repaintItems();
    void setSelectedIndex(int i);
    int  getSelectedIndex() const noexcept { return selectedIndex; }

    std::function<void(int)> onRhythmSelected;
    std::function<void()>    onAddRhythm;
    std::function<void(int)> onRhythmsReordered;

    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;

private:
    PluginProcessor& proc;

    juce::Viewport  viewport;
    juce::Component itemContainer;
    std::vector<std::unique_ptr<SidebarItem>> items;
    AddButton addButton { "Rhythm" };
    juce::ComponentAnimator animator;

    int selectedIndex = 0;

    static constexpr int kItemH          = 80;
    static constexpr int kAddBtnH        = 34;
    static constexpr int kDragThresholdPx = 4;

    enum class DragPhase { Idle, Dragging };
    DragPhase            dragPhase = DragPhase::Idle;
    int                  dragSourceIndex = -1;
    int                  dragTargetIndex = -1;
    juce::Image          dragGhostImage;
    juce::Rectangle<int> dragGhostBounds;

    void layoutItems(bool animate, int newItemIndex = -1);

    void onItemDragStart(int sourceIndex, const juce::MouseEvent& e);
    void onItemDragMove (int sourceIndex, const juce::MouseEvent& e);
    void onItemDragEnd  (int sourceIndex, const juce::MouseEvent& e);
    int  computeTargetIndex(int containerLocalY) const;
    void commitDrag();
    void cancelDrag();
    void timerCallback() override;
};
