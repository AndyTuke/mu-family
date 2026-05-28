#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/Components/MuLookAndFeel.h"

// One entry in a ChannelSidebar — a single "layer" (rhythm in mu-clid, voice in
// mu-tant). Product-agnostic: the per-layer mini-graphic is a Component the
// product injects (mu-clid → a RhythmCircle wrapper; mu-tant → its own voice
// graphic). The item itself owns the shared chrome: selected background + tab
// line, colour dot + name, hit-pulse ring, pending-swap badge, and
// drag-to-reorder. The animation inside the mini-graphic is the product's; the
// UX around it is identical across the family.
class SidebarItem : public juce::Component, private juce::Timer
{
public:
    explicit SidebarItem(int index);
    ~SidebarItem() override { stopTimer(); }

    void setIndex(int i)               { index = i; }
    int  getIndex() const noexcept     { return index; }
    void setName(const juce::String& n);
    void setColour(juce::Colour c);
    void setSelected(bool s);
    void setPendingSwap(bool p);

    // Flash the hit-pulse ring (product calls this on a layer event, e.g. a
    // mu-clid step hit). No-op cost when idle.
    void pulse();

    // Take ownership of the product-supplied mini-graphic (nullptr clears it).
    void setMiniVisual(std::unique_ptr<juce::Component> visual);
    juce::Component* getMiniVisual() const noexcept { return miniVisual.get(); }

    std::function<void(int)> onSelected;
    std::function<void(int)> onCancelPendingSwap;
    std::function<void(int, const juce::MouseEvent&)> onDragStart;
    std::function<void(int, const juce::MouseEvent&)> onDragMove;
    std::function<void(int, const juce::MouseEvent&)> onDragEnd;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp  (const juce::MouseEvent&) override;

private:
    int          index;
    juce::String name   { "---" };
    juce::Colour colour { juce::Colours::grey };
    bool         selected    = false;
    bool         pendingSwap = false;
    float        pulseAlpha  = 0.0f;

    juce::Point<int> mouseDownPos;
    bool             isDragging = false;

    std::unique_ptr<juce::Component> miniVisual;

    juce::Rectangle<int> badgeBounds() const;
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SidebarItem)
};
