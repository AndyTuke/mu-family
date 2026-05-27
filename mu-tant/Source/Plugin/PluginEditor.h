#pragma once

#include "Plugin/ProcessorBase.h"
#include "UI/EditorShellBase.h"
#include "Plugin/PluginProcessor.h"
#include "UI/VoicePanel.h"
#include "UI/VoiceSidebar.h"

namespace mu_tant
{

// mu-Tant editor: shared mu-core shell + mu-tant-specific sidebar (8 voice
// buttons) and main panel (per-voice synth UI). No mixer overlay or settings
// overlay yet (mixer comes in stage A3; settings in a later phase). Stage A1+A2
// wires multi-voice selection so clicking a voice in the sidebar rebinds the
// VoicePanel to that voice's APVTS subtree.
class PluginEditor : public EditorShellBase
{
public:
    explicit PluginEditor(PluginProcessor&);
    ~PluginEditor() override = default;

private:
    PluginProcessor& proc;
    VoiceSidebar     voiceSidebar;
    VoicePanel       voicePanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};

} // namespace mu_tant
