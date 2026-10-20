#pragma once

#include "UI/EditorShellBase.h"
#include "UI/LayerSidebar.h"     // mu-core shared sidebar (select/add/delete/reorder)
#include "UI/LayerEditController.h"   // mu-core: add / delete / reorder glue
#include "UI/MixerOverlay.h"       // mu-core shared mixer
#include "Plugin/PluginProcessor.h"
#include "UI/LayerPanel.h"        // mu-core shared per-layer panel shell
#include "UI/ArpContent.h"
#include "UI/Components/LayerDiscVisual.h"   // mu-core: the sidebar disc
#include "Modulation/MuToniModDest.h"
#include "UI/SettingsOverlay.h"

namespace mu_toni
{

// mu-Toni editor: the shared mu-core shell (TransportBar / StatusBar / About /
// overlays / window sizing / MuLookAndFeel) + the shared LayerSidebar + shared
// MixerOverlay, around the shared LayerPanel (preset strip + modulators) holding the
// ArpContent band (arp / osc / filter / env / insert controls). No bespoke shell or mixer
// code — the product supplies only the sidebar visual and the content band.
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
    PluginProcessor& proc;
    LayerSidebar   sidebar;
    ModDestProvider  modDestProvider = makeModDestProvider();
    ArpContent       arpContent;
    LayerPanel       layerPanel;
    mu_ui::LayerEditController layerEdit;   // add / delete / reorder glue; after the sidebar and panel
    MixerOverlay     mixerOverlay;
    SettingsOverlay  settingsOverlay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};

} // namespace mu_toni
