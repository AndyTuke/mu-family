#pragma once

#include "UI/EditorShellBase.h"
#include "UI/ChannelSidebar.h"     // mu-core shared sidebar (select/add/delete/reorder)
#include "UI/MixerOverlay.h"       // mu-core shared mixer
#include "Plugin/PluginProcessor.h"
#include "UI/EnginePanel.h"

namespace mu_toni
{

// mu-Toni editor: the shared mu-core shell (TransportBar / StatusBar / About /
// overlays / window sizing / MuLookAndFeel) + the shared ChannelSidebar + shared
// MixerOverlay, around a blank EnginePanel that marks where the product-specific
// synth engine + sequencer UI will go. No bespoke shell or mixer code — that's
// the whole point of the scaffold: the platform is "done", only the engine is TBD.
class PluginEditor : public EditorShellBase
{
public:
    explicit PluginEditor(PluginProcessor&);
    ~PluginEditor() override = default;

private:
    PluginProcessor& proc;
    ChannelSidebar   sidebar;
    EnginePanel      enginePanel;
    MixerOverlay     mixerOverlay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};

} // namespace mu_toni
