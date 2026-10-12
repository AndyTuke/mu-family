#include "PluginEditor.h"

namespace mu_tant
{

PluginEditor::PluginEditor(PluginProcessor& p)
    : EditorShellBase(p),
      proc(p),
      sidebar(p, "Voice"),
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

    // Hot-swap commit refresh. A staged full preset lands at the loop boundary and the shell's
    // onPresetSwapCommitted handler (set in EditorShellBase) calls onPresetLoaded once every apply
    // is done; a staged per-voice preset refreshes through this callback.
    proc.onLayerPresetCommitted = [this](int v)
    {
        sidebar.refreshItems();              // glyph / colour may have changed
        if (voicePanel.getVoice() == v)
            voicePanel.setVoice(v);               // re-read knobs + wavetable dropdowns
    };

    // Main area + mixer overlay (Stage A3 — channel strip lvl/pan/mute/solo
    // bound via apvts; FX send / sidechain knobs are visible but inert until
    // MixerEngine accepts a per-voice render callback).
    setMainArea(&sidebar, &voicePanel);
    setMixerOverlay(&mixerOverlay);

    // Per-voice spectrum glyph; its level pulse flashes the sidebar item.
    sidebar.createMiniVisual = [this](int i, std::function<void()> pulse) -> std::unique_ptr<juce::Component>
    {
        const auto col = MuLookAndFeel::channelPalette[
            (size_t) (proc.getChannelColourIndex(i) % MuLookAndFeel::kChannelPaletteSize)];
        auto glyph = std::make_unique<PatternMiniVisual>(col, &proc.getPattern(i).ring);
        glyph->onPulse = std::move(pulse);
        return glyph;
    };
    sidebar.refreshItems();

    // Sidebar select / add / delete / reorder — same UX as mu-clid's rhythms.
    sidebar.onLayerSelected = [this](int idx) { voicePanel.setVoice(idx); };

    sidebar.onAddLayer = [this]
    {
        const int idx = proc.addLayer();
        if (idx < 0) return;                       // already at the 8-voice max
        sidebar.refreshItems();
        sidebar.setSelectedIndex(idx);
        voicePanel.setVoice(idx);
    };

    sidebar.onLayersReordered = [this](int newSelected)
    {
        voicePanel.setVoice(newSelected);
    };

    voicePanel.onDeleteVoice = [this]
    {
        if (proc.getNumVoices() <= 1) return;      // never delete the last voice
        const int idx = voicePanel.getVoice();
        // Unbind everything that points into the voice's Pattern before it is destroyed (the edit
        // moves the Pattern pointers and frees the removed one), so no timer or paint callback can
        // dereference it in the window before setVoice rebinds.
        voicePanel.clearModulatorSlot();
        voicePanel.clearAllModBindings();
        voicePanel.clearPatternBindings();
        proc.removeLayer(idx);
        const int newIndex = juce::jlimit(0, proc.getNumVoices() - 1, idx);
        sidebar.refreshItems();
        sidebar.setSelectedIndex(newIndex);
        voicePanel.setVoice(newIndex);
    };

    // Forward mixer status updates to the shared StatusBar.

    sidebar.setSelectedIndex(0);
    voicePanel.setVoice(0);
    mixerOverlay.loadFromApvts();
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
    proc.onLayerPresetCommitted = nullptr;
}

void PluginEditor::onPresetLoaded(const juce::File&)
{
    sidebar.refreshItems();
    sidebar.setSelectedIndex(0);
    voicePanel.setVoice(0);         // re-reads insert algo + slot knobs from APVTS
    mixerOverlay.loadFromApvts();
}

void PluginEditor::onPresetNew()
{
    sidebar.refreshItems();
    sidebar.setSelectedIndex(0);
    voicePanel.setVoice(0);
    mixerOverlay.loadFromApvts();
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor(*this);
}

} // namespace mu_tant
