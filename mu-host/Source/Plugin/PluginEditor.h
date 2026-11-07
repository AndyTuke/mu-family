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

    PluginProcessor& hostProc;
    EngineHolder     engineHolder;
    DropdownSelect&  engineBox = engineHolder.box;
    std::vector<mu_core::LayerTypeId> engineTypes;
    Refresher        refresher;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};

} // namespace mu_host
