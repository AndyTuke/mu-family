#pragma once

#include "Plugin/ProcessorBase.h"
#include "UI/EditorShellBase.h"
#include "Plugin/PluginProcessor.h"
#include "UI/VoicePanel.h"

namespace mu_tant
{

// First-stab editor for mu-Tant. Extends the shared mu-core shell with a
// single voice panel (mu-core knob style, APVTS-bound). No sidebar (single-
// layer for now), no mixer overlay (single voice), no settings overlay (no
// per-product settings yet), no preset library (the engine isn't preset-aware
// in this stab). The shell handles LookAndFeel + transport chrome + about +
// status bar — same window design as mu-Clid by construction.
class PluginEditor : public EditorShellBase
{
public:
    explicit PluginEditor(PluginProcessor&);
    ~PluginEditor() override = default;

private:
    PluginProcessor& proc;
    VoicePanel       voicePanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};

} // namespace mu_tant
