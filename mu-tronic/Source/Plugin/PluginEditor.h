#pragma once

#include "UI/EditorShellBase.h"
#include "UI/LayerSidebar.h"          // mu-core shared sidebar (select / delete / pages)
#include "UI/LayerEditController.h"   // mu-core: delete glue
#include "UI/MixerOverlay.h"          // mu-core shared mixer
#include "UI/LayerPanel.h"            // mu-core shared per-layer panel shell
#include "Plugin/PluginProcessor.h"
#include "UI/EmptySlotChooser.h"
#include "UI/TronicSettingsOverlay.h"

namespace mu_tronic
{

// mu-Tronic editor: the shared mu-core shell (TransportBar / StatusBar / About / overlays / window sizing /
// MuLookAndFeel) + the shared LayerSidebar (paged, eight slots at a time) + the shared MixerOverlay (its strips paged
// the same way), around the shared LayerPanel (preset strip + modulators). An Empty slot shows the chooser in the
// content area; a slot that holds a layer shows that type's content. No bespoke shell or mixer code.
class PluginEditor : public EditorShellBase
{
public:
    explicit PluginEditor(PluginProcessor&);
    ~PluginEditor() override;

protected:
    // Shell hooks: after a full preset loads (or New), re-read every panel from the state.
    void onPresetLoaded(const juce::File& file) override;
    void onPresetNew() override;

private:
    // Shows slot `idx` in the main area: the chooser while it is Empty, else the type's content.
    void showSlot(int idx);
    // The slot's layer was given a type: rebuild what depends on it.
    void layerTypeSet(int slot);

    PluginProcessor& proc;
    LayerSidebar   sidebar;
    EmptySlotChooser chooser;
    LayerPanel       layerPanel;
    mu_ui::LayerEditController layerEdit;   // delete glue; after the sidebar and panel
    MixerOverlay     mixerOverlay;
    TronicSettingsOverlay settingsOverlay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};

} // namespace mu_tronic
