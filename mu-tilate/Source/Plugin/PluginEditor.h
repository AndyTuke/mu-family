#pragma once

#include "Plugin/PluginProcessor.h"
#include "UI/EditorShellBase.h"
#include "UI/LayerEditController.h"          // mu-core: add / delete / reorder glue
#include "UI/LayerPanel.h"                   // mu-core shared per-layer panel shell
#include "UI/LayerSidebar.h"                 // mu-core shared sidebar (select/add/delete/reorder)
#include "UI/MixerOverlay.h"                 // mu-core shared mixer
#include "UI/PhraseContent.h"
#include "UI/PhraseModDest.h"
#include "UI/StandardSettingsOverlay.h"      // mu-core: the family-standard settings page

namespace mu_tilate
{

// mu-Tilate editor: the shared mu-core shell (TransportBar / StatusBar / About / overlays / window sizing / MuLookAndFeel)
// plus the shared LayerSidebar and MixerOverlay, around the shared LayerPanel (preset strip + modulators) holding the
// PhraseContent band. The product supplies only the sidebar visual and the content band.
class PluginEditor : public EditorShellBase
{
public:
    explicit PluginEditor(PluginProcessor&);
    ~PluginEditor() override;

protected:
    void onPresetLoaded(const juce::File& file) override;
    void onPresetNew() override;

private:
    PluginProcessor& proc;
    LayerSidebar     sidebar;
    ModDestProvider  modDestProvider = makeModDestProvider();
    PhraseContent    content;
    LayerPanel       layerPanel;
    mu_ui::LayerEditController layerEdit;   // after the sidebar and panel
    MixerOverlay     mixerOverlay;
    mu_ui::StandardSettingsOverlay settingsOverlay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};

} // namespace mu_tilate
