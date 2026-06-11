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
    getSaveDialog().setShowEmbedSamples(false);   // mu-tant has no sample engine
    getAboutPanel().setProductInfo(
        juce::String(juce::CharPointer_UTF8("\xce\xbc")) + "-Tant",
        juce::StringArray {
            juce::String(juce::CharPointer_UTF8("JUCE \xe2\x80\x94 Proprietary (JUCE 7 license)")),
            juce::String(juce::CharPointer_UTF8("Signalsmith Reverb \xe2\x80\x94 MIT")),
            juce::String(juce::CharPointer_UTF8("clap-juce-extensions \xe2\x80\x94 MIT")),
        });
    getActivationPanel().setProductName(juce::String(juce::CharPointer_UTF8("\xce\xbc-Tant")));
    getTransportBar().setLogoText(juce::String(juce::CharPointer_UTF8("\xce\xbc-Tant")));

    // Preset library — full presets via the shared shell chrome (TransportBar
    // dropdown + Save dialog + Preset browser). The processor implements the
    // save/load + directories; the shell drives the UI.
    getTransportBar().setShowPresetControls(true);
    getTransportBar().refreshPresets();

    // Basic settings page (master vol + UI size + BPM) behind the gear button.
    settingsOverlay.onClose = [this] { showSettings(false); };
    // MIDI program-change tables — open the shared mu-core overlays (Ch 1-8 →
    // per-voice presets, Ch 9 → full presets); their onClose returns to settings.
    settingsOverlay.onMidiPresetsClicked = [this] { showSettings(false); showMidiPresets(true); };
    settingsOverlay.onFullPresetsClicked = [this] { showSettings(false); showMidiFullPresets(true); };
    setSettingsOverlay(&settingsOverlay);

    // Hot-swap commit refresh. A staged preset is applied on the message thread
    // at the loop boundary (after the shell's synchronous onPresetLoaded has
    // already run against the pre-swap state), so re-run the refresh here.
    proc.onPresetSwapCommitted = [this] { onPresetLoaded({}); };
    proc.onVoiceHotSwapCommitted = [this](int v)
    {
        voiceSidebar.refreshItems();              // glyph / colour may have changed
        if (voicePanel.getVoice() == v)
            voicePanel.setVoice(v);               // re-read knobs + wavetable dropdowns
    };

    // Main area + mixer overlay (Stage A3 — channel strip lvl/pan/mute/solo
    // bound via apvts; FX send / sidechain knobs are visible but inert until
    // MixerEngine accepts a per-voice render callback).
    setMainArea(&voiceSidebar, &voicePanel);
    setMixerOverlay(&mixerOverlay);

    // Sidebar select / add / delete / reorder — same UX as mu-clid's rhythms.
    voiceSidebar.onChannelSelected = [this](int idx) { voicePanel.setVoice(idx); };

    voiceSidebar.onAddChannel = [this]
    {
        if (!proc.canAddChannel())
        {
            getStatusBar().showParam("Demo", juce::String::fromUTF8(u8"Demo limited to 1 voice — purchase a license to unlock all 8"),
                                     MuLookAndFeel::colour(MuLookAndFeel::knobLevel));
            return;
        }
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
        // Null out modulator panel pointer before the slot data is shifted so no
        // timer or paint callback can dereference a stale slot during the window.
        voicePanel.clearModulatorSlot();
        voicePanel.clearAllModBindings();
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

PluginEditor::~PluginEditor()
{
    // The processor can outlive the editor (DAW close-window-keep-plugin); clear
    // the hot-swap callbacks so a boundary commit can't fire into a dead editor.
    proc.onPresetSwapCommitted   = nullptr;
    proc.onVoiceHotSwapCommitted = nullptr;
}

void PluginEditor::onPresetLoaded(const juce::File&)
{
    voiceSidebar.refreshItems();
    voiceSidebar.setSelectedIndex(0);
    voicePanel.setVoice(0);         // re-reads insert algo + slot knobs from APVTS
    mixerOverlay.loadFromAPVTS();
}

void PluginEditor::onPresetNew()
{
    voiceSidebar.refreshItems();
    voiceSidebar.setSelectedIndex(0);
    voicePanel.setVoice(0);
    mixerOverlay.loadFromAPVTS();
}

} // namespace mu_tant
