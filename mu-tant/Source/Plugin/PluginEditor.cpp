#include "PluginEditor.h"

namespace mu_tant
{

PluginEditor::PluginEditor(PluginProcessor& p)
    : EditorShellBase(p),
      proc(p),
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

    // First-stab scope: no preset library, no mixer, no settings overlay.
    // Shell auto-hides the mixer toggle when no mixer overlay is registered;
    // the gear button stays visible but is a no-op until settings land.
    getTransportBar().setShowPresetControls(false);
    setMixerOverlay(nullptr);
    setSettingsOverlay(nullptr);

    // Main area: voice panel only, no sidebar (single layer for now).
    setMainArea(/*sidebar=*/nullptr, &voicePanel);
    clearPresetDirty();
}

} // namespace mu_tant
