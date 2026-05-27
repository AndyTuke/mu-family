#include "PluginEditor.h"

namespace mu_tant
{

PluginEditor::PluginEditor(PluginProcessor& p)
    : EditorShellBase(p),
      proc(p),
      voiceSidebar(p),
      voicePanel(p)
{
    // ── Product chrome on shared overlays ───────────────────────────────────
    getAboutPanel().setProductInfo(
        juce::String(juce::CharPointer_UTF8("\xce\xbc")) + "-Tant",
        juce::StringArray {
            juce::String(juce::CharPointer_UTF8("JUCE \xe2\x80\x94 Proprietary (JUCE 7 license)")),
            juce::String(juce::CharPointer_UTF8("Signalsmith Reverb \xe2\x80\x94 MIT")),
            juce::String(juce::CharPointer_UTF8("clap-juce-extensions \xe2\x80\x94 MIT")),
        });
    getTransportBar().setLogoText(juce::String(juce::CharPointer_UTF8("\xce\xbc-Tant")));

    // First-stab scope: no preset library, no settings overlay yet. Mixer
    // overlay lands in Stage A3.
    getTransportBar().setShowPresetControls(false);
    setMixerOverlay(nullptr);
    setSettingsOverlay(nullptr);

    // Main area: 8-voice sidebar + per-voice editor panel.
    setMainArea(&voiceSidebar, &voicePanel);

    voiceSidebar.onVoiceSelected = [this](int idx)
    {
        voicePanel.setVoice(idx);
    };

    voiceSidebar.setSelectedIndex(0);
    voicePanel.setVoice(0);
    clearPresetDirty();
}

} // namespace mu_tant
