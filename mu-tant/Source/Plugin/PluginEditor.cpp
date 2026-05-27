#include "PluginEditor.h"

namespace mu_tant
{

PluginEditor::PluginEditor(PluginProcessor& p)
    : EditorShellBase(p),
      proc(p),
      voiceSidebar(p),
      voicePanel(p),
      mixerOverlay(p, p.mixerEngine)
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

    // First-stab scope: no preset library, no settings overlay yet.
    getTransportBar().setShowPresetControls(false);
    setSettingsOverlay(nullptr);

    // Main area + mixer overlay (Stage A3 — channel strip lvl/pan/mute/solo
    // bound via apvts; FX send / sidechain knobs are visible but inert until
    // MixerEngine accepts a per-voice render callback).
    setMainArea(&voiceSidebar, &voicePanel);
    setMixerOverlay(&mixerOverlay);

    voiceSidebar.onVoiceSelected = [this](int idx)
    {
        voicePanel.setVoice(idx);
    };

    // Forward mixer status updates to the shared StatusBar.
    mixerOverlay.onStatusUpdate = [this](const juce::String& name,
                                          const juce::String& val,
                                          juce::Colour col)
    {
        getStatusBar().showParam(name, val, col);
    };

    voiceSidebar.setSelectedIndex(0);
    voicePanel.setVoice(0);
    mixerOverlay.loadFromAPVTS();
    clearPresetDirty();
}

} // namespace mu_tant
