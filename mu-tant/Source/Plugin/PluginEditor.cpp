#include "PluginEditor.h"

namespace mu_tant
{

PluginEditor::PluginEditor(PluginProcessor& p)
    : EditorShellBase(p),
      proc(p),
      voiceSidebar(p),
      voicePanel(p),
      mixerOverlay(p, p.mixerEngine),
      settingsOverlay(p),
      masterLoop(p)
{
    // ── Product chrome on shared overlays ───────────────────────────────────
    getSaveDialog().setShowEmbedSamples(false);   // mu-tant has no sample engine
    setProductIdentity(juce::String(juce::CharPointer_UTF8("\xce\xbc-Tant")));

    // Master-loop sub-pane in the transport (shared mu-core MasterLoopSection):
    // Loop-length dropdown + step counter driven by the mstrLoop param + beat pos.
    getTransportBar().setLoopSection(&masterLoop, MasterLoopSection::kWidth);
    masterLoop.onStatusUpdate = [this](const juce::String& name, const juce::String& val)
    {
        getStatusBar().showParam(name, val);
    };

    // Preset library — full presets via the shared shell chrome (TransportBar
    // dropdown + Save dialog + Preset browser). The processor implements the
    // save/load + directories; the shell drives the UI.
    getTransportBar().setShowPresetControls(true);
    getTransportBar().refreshPresets();

    // Basic settings page (master vol + UI size + BPM) behind the gear button.
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
        const int newIndex = juce::jlimit(0, proc.getNumVoices() - 1, idx);
        voiceSidebar.refreshItems();
        voiceSidebar.setSelectedIndex(newIndex);
        voicePanel.setVoice(newIndex);
    };

    // Forward mixer status updates to the shared StatusBar.

    voiceSidebar.setSelectedIndex(0);
    voicePanel.setVoice(0);
    mixerOverlay.loadFromAPVTS();
    clearPresetDirty();

    // The family metal look, in mu-Tant's green.
    setMetalStyle(true, MuLookAndFeel::colour(MuLookAndFeel::appGreen));
    setScrews(true);

    // Every control on the main panel shows its name + value on the status bar.
    forwardKnobStatus(voicePanel);
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
