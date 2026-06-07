#pragma once

#include "UI/EditorShellBase.h"
#include "UI/ChannelSidebar.h"     // mu-core shared sidebar
#include "UI/MixerOverlay.h"       // mu-core shared mixer
#include "Plugin/PluginProcessor.h"
#include "UI/EnginePanel.h"

namespace mu_on
{

// mu-On editor: the shared mu-core shell (TransportBar / StatusBar / About / overlays /
// window sizing / MuLookAndFeel) + the shared ChannelSidebar (the four instrument lanes)
// + shared MixerOverlay, around the EnginePanel (909 grid + engine controls land next).
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

} // namespace mu_on
