#pragma once

#include "Plugin/ProcessorBase.h"
#include "UI/EditorShellBase.h"
#include "UI/MixerOverlay.h"
#include "Plugin/PluginProcessor.h"
#include "UI/VoicePanel.h"
#include "UI/VoiceSidebar.h"

namespace mu_tant
{

// mu-Tant editor: shared mu-core shell + mu-tant-specific sidebar (8 voice
// buttons), main panel (per-voice synth UI), and mixer overlay (per-channel
// level/pan/mute/solo via the shared MixerOverlay; FX sends + sidechain UI
// is present but inert until the MixerEngine voice-render-callback refactor
// lets mu-tant route through the shared mixer / FX path). No settings overlay
// yet (gear button is currently a no-op).
class PluginEditor : public EditorShellBase
{
public:
    explicit PluginEditor(PluginProcessor&);
    ~PluginEditor() override = default;

private:
    PluginProcessor& proc;
    VoiceSidebar     voiceSidebar;
    VoicePanel       voicePanel;
    MixerOverlay     mixerOverlay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};

} // namespace mu_tant
