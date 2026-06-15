#pragma once

#include "UI/EditorShellBase.h"
#include "UI/ChannelSidebar.h"     // mu-core shared sidebar
#include "UI/MixerOverlay.h"       // mu-core shared mixer
#include "Plugin/PluginProcessor.h"
#include "UI/GroovePanel.h"
#include "UI/SettingsOverlay.h"

#include <array>

namespace mu_on
{

// mu-On editor: the shared mu-core shell (TransportBar / StatusBar / About / overlays /
// window sizing / MuLookAndFeel) + the shared ChannelSidebar (the four instrument lanes)
// + shared MixerOverlay, around the GroovePanel (909 grid + per-channel engine controls).
class PluginEditor : public EditorShellBase
{
public:
    explicit PluginEditor(PluginProcessor&);
    ~PluginEditor() override = default;

private:
    PluginProcessor& proc;
    ChannelSidebar   sidebar;
    GroovePanel      groovePanel;
    MixerOverlay     mixerOverlay;
    SettingsOverlay  settingsOverlay;

    // Last-seen per-lane trigger counts — diffed each animation tick to pulse the sidebar.
    std::array<int, kNumChannels> lastTriggers { };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};

} // namespace mu_on
