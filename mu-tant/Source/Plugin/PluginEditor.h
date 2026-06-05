#pragma once

#include "Plugin/ProcessorBase.h"
#include "UI/EditorShellBase.h"
#include "UI/MixerOverlay.h"
#include "Plugin/PluginProcessor.h"
#include "UI/VoicePanel.h"
#include "UI/VoiceSidebar.h"
#include "UI/SettingsOverlay.h"

namespace mu_tant
{

// mu-Tant editor: shared mu-core shell + mu-tant-specific sidebar (8 voice
// buttons), main panel (per-voice synth UI), mixer overlay (full FX + sidechain
// rack via the shared MixerOverlay), and settings overlay (gear button).
class PluginEditor : public EditorShellBase
{
public:
    explicit PluginEditor(PluginProcessor&);
    ~PluginEditor() override;

    // Refresh the voice panel (insert algo + knobs) and mixer after a full preset load.
    void onPresetLoaded(const juce::File&) override;
    void onPresetNew()                     override;

private:
    PluginProcessor& proc;
    VoiceSidebar     voiceSidebar;
    VoicePanel       voicePanel;
    MixerOverlay     mixerOverlay;
    SettingsOverlay  settingsOverlay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};

} // namespace mu_tant
