#pragma once

#include "UI/EditorShellBase.h"
#include "UI/LayerSidebar.h"          // mu-core shared sidebar (select/add/delete/reorder)
#include "UI/LayerEditController.h"   // mu-core: add / delete / reorder glue
#include "UI/LayerPanel.h"            // mu-core shared per-layer panel shell
#include "UI/Components/LayerDiscVisual.h"   // mu-core: the sidebar disc
#include "UI/MixerOverlay.h"          // mu-core shared mixer
#include "Plugin/PluginProcessor.h"
#include "Modulation/MuOnModDest.h"
#include "UI/TrackContent.h"
#include "UI/SettingsOverlay.h"

#include <array>

namespace mu_on
{

// mu-On editor: the shared mu-core shell (TransportBar / StatusBar / About / overlays / window
// sizing / MuLookAndFeel) + the shared LayerSidebar (one to eight tracks) + shared MixerOverlay, around
// the shared LayerPanel (preset strip + modulators) holding the TrackContent band (the shown track's
// engine and the step rows). No bespoke shell or mixer code — the product supplies only the sidebar
// visual and the content band.
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
    void showTrack(int idx);          // select track idx everywhere (sidebar, panel, content)
    void engineChanged();             // the shown track's engine changed: names, mini visuals, destinations
    void useDestinationsOf(int idx);  // the modulation destinations of track idx's engine

    PluginProcessor& proc;
    LayerSidebar   sidebar;
    std::array<ModDestProvider, kNumEngineTypes> modProviders;   // one destination set per engine
    TrackContent   trackContent;
    LayerPanel     layerPanel;
    mu_ui::LayerEditController layerEdit;   // add / delete / reorder glue; after the sidebar and panel
    MixerOverlay   mixerOverlay;
    SettingsOverlay settingsOverlay;

    // Last-seen per-track trigger counts — diffed each animation tick to pulse the sidebar.
    std::array<int, mu_limits::kMaxLayers> lastTriggers { };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};

} // namespace mu_on
