#pragma once

#include "UI/EditorShellBase.h"
#include "UI/ChannelSidebar.h"     // mu-core shared sidebar (select/add/delete/reorder)
#include "UI/MixerOverlay.h"       // mu-core shared mixer
#include "Plugin/PluginProcessor.h"
#include "UI/EnginePanel.h"
#include "UI/SettingsOverlay.h"

namespace mu_toni
{

// mu-Toni editor: the shared mu-core shell (TransportBar / StatusBar / About /
// overlays / window sizing / MuLookAndFeel) + the shared ChannelSidebar + shared
// MixerOverlay, around the EnginePanel carrying the arp / osc / filter / env /
// insert controls and the modulation band. No bespoke shell or mixer code — the
// product supplies only the sidebar visual and the engine panel.
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
    ChannelSidebar   sidebar;
    EnginePanel      enginePanel;
    MixerOverlay     mixerOverlay;
    SettingsOverlay  settingsOverlay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};

} // namespace mu_toni
