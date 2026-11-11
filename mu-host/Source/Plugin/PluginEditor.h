#pragma once

#include <Plugin/PluginEditor.h>   // mu-Tronic's editor (the shell), found through the include path: never this file
#include "PluginProcessor.h"
#include "UI/Components/DropdownSelect.h"

#include <vector>

namespace mu_host
{

// The mu-Host editor: the mu-Tronic shell in its one-slot form (no sidebar, scenes, clips, mixer or full-preset controls), with the
// engine drop-down in the transport bar and the licence of the engine's product. The window is the standard one less the sidebar's
// width, so the voice panels keep their standard size and the sidebar is simply cropped off the side.
class PluginEditor : public mu_tronic::PluginEditor
{
public:
    explicit PluginEditor(PluginProcessor&);
    ~PluginEditor() override;

protected:
    // An engine was opened (a choice, a restore): its product's licence check, and the drop-down shows it.
    void slotShown(int slot, mu_core::LayerTypeId type) override;

private:
    // The drop-down sits in a holder that fills the transport bar's loop pane and leaves it a margin, as the other products' loop
    // sections do.
    struct EngineHolder : juce::Component
    {
        DropdownSelect box;
        EngineHolder() { addAndMakeVisible(box); }
        void resized() override { box.setBounds(getLocalBounds().reduced(mu_ui::s(6), mu_ui::s(4))); }
    };
    // Four times a second the drop-down is brought in line with the slot (a state restore changes the engine behind its back).
    struct Refresher : juce::Timer
    {
        std::function<void()> tick;
        void timerCallback() override { if (tick) tick(); }
    };

    void refreshEngineBox();
    // Refills the drop-down with the engines of the products the user holds a key for (plus the engine already in the slot, so a
    // saved project still shows what it holds); a no-op when that list has not changed.
    void rebuildEngineList();
    // Free / demo mode (no product licensed, nothing in the slot): explains what mu-Host is and why the list is empty.
    void maybeShowNoEnginesNotice();

    PluginProcessor& hostProc;
    EngineHolder     engineHolder;
    DropdownSelect&  engineBox = engineHolder.box;
    std::vector<mu_core::LayerTypeId> allEngineTypes;   // every engine, in the chooser's order
    std::vector<mu_core::LayerTypeId> engineTypes;      // the ones the drop-down lists now (drop-down id = index + 1)
    bool engineListBuilt = false;
    int  tickCount = 0;
    bool noticeChecked = false;
    Refresher        refresher;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};

} // namespace mu_host
