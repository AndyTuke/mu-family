#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "SidebarItem.h"
#include "Components/AddButton.h"
#include "Components/MuClidLookAndFeel.h"
#include "../PluginProcessor.h"

// Left sidebar (~82px). One SidebarItem per rhythm, scrollable if needed.
// Add rhythm button at bottom. Variable ordering supported for future drag-to-reorder.
class RhythmSidebar : public juce::Component
{
public:
    static constexpr int kWidth = 82;

    explicit RhythmSidebar(PluginProcessor& p);

    void refreshItems();
    void setSelectedIndex(int i);
    int  getSelectedIndex() const noexcept { return selectedIndex; }

    std::function<void(int)> onRhythmSelected;
    std::function<void()>    onAddRhythm;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    PluginProcessor& proc;

    juce::Viewport  viewport;
    juce::Component itemContainer;
    std::vector<std::unique_ptr<SidebarItem>> items;
    AddButton addButton { "Rhythm" };

    int selectedIndex = 0;

    static constexpr int kItemH   = 80;
    static constexpr int kAddBtnH = 34;
};
