#include "PluginEditor.h"

namespace mu_tant
{

PluginEditor::PluginEditor(PluginProcessor& p)
    : EditorShellBase(p),
      proc(p),
      voiceSidebar(p),
      voicePanel(p),
      mixerOverlay(p, p.mixerEngine),
      settingsOverlay(p)
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

    // Preset library — full presets via the shared shell chrome (TransportBar
    // dropdown + Save dialog + Preset browser). The processor implements the
    // save/load + directories; the shell drives the UI.
    getTransportBar().setShowPresetControls(true);
    getTransportBar().refreshPresets();

    // Basic settings page (master vol + UI size + BPM) behind the gear button.
    settingsOverlay.onClose = [this] { showSettings(false); };
    setSettingsOverlay(&settingsOverlay);

    // Main area + mixer overlay (Stage A3 — channel strip lvl/pan/mute/solo
    // bound via apvts; FX send / sidechain knobs are visible but inert until
    // MixerEngine accepts a per-voice render callback).
    setMainArea(&voiceSidebar, &voicePanel);
    setMixerOverlay(&mixerOverlay);

    // Sidebar select / add / delete / reorder — same UX as mu-clid's rhythms.
    voiceSidebar.onChannelSelected = [this](int idx) { voicePanel.setVoice(idx); };

    voiceSidebar.onAddChannel = [this]
    {
        const int idx = proc.addVoice();
        if (idx < 0) return;                       // already at the 8-voice max
        voiceSidebar.refreshItems();
        voiceSidebar.setSelectedIndex(idx);
        voicePanel.setVoice(idx);
    };

    voiceSidebar.onChannelsReordered = [this](int newSelected)
    {
        voicePanel.setVoice(newSelected);
    };

    voicePanel.onDeleteVoice = [this]
    {
        if (proc.getNumVoices() <= 1) return;      // never delete the last voice
        const int idx = voicePanel.getVoice();
        proc.removeVoice(idx);
        const int newIdx = juce::jlimit(0, proc.getNumVoices() - 1, idx);
        voiceSidebar.refreshItems();
        voiceSidebar.setSelectedIndex(newIdx);
        voicePanel.setVoice(newIdx);
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
