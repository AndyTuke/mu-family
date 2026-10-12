#include "PluginEditor.h"

namespace mu_tant
{

PluginEditor::PluginEditor(PluginProcessor& p)
    : EditorShellBase(p),
      proc(p),
      sidebar(p, "Voice"),
      patternContent(p),
      layerPanel(p, "Voice"),
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
        if (layerPanel.getLayer() == v)
            layerPanel.setLayer(v);               // re-read knobs + wavetable dropdowns
    };

    // Main area + mixer overlay (Stage A3 — channel strip lvl/pan/mute/solo
    // bound via apvts; FX send / sidechain knobs are visible but inert until
    // MixerEngine accepts a per-voice render callback).
    setMainArea(&sidebar, &layerPanel);
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

    // The shared layer panel: the pattern content band in the middle, mu-Tant's modulation
    // destinations; a layer change rebinds the content's attachments.
    layerPanel.setContent(&patternContent);
    layerPanel.setDestProvider(&modDestProvider);
    layerPanel.setDefaultPresetName([](int v) { return "Voice " + juce::String(v + 1); });
    layerPanel.onLayerChanged = [this](int layer) { patternContent.setLayer(layer); };
    // Re-label the modulators' insert destinations to the active algorithm's slot names.
    patternContent.onInsertAlgorithmChanged = [this](int charId)
    {
        layerPanel.getModulatorPanel().setInsertAlgorithm(charId);
    };
    // A modulator edit (assignment/mode/dest change) may flip whether a Stepped CS drives
    // pitch — refresh the processor's cached flags for the shown voice (message thread).
    layerPanel.getModulatorPanel().onChange = [this] { proc.refreshPitchQuantFlags(layerPanel.getLayer()); };

    // Sidebar select / add / delete / reorder — same UX as mu-clid's rhythms.
    sidebar.onLayerSelected = [this](int idx) { layerPanel.setLayer(idx); };

    sidebar.onAddLayer = [this]
    {
        const int idx = proc.addLayer();
        if (idx < 0) return;                       // already at the 8-voice max
        sidebar.refreshItems();
        sidebar.setSelectedIndex(idx);
        layerPanel.setLayer(idx);
    };

    sidebar.onLayersReordered = [this](int newSelected)
    {
        layerPanel.setLayer(newSelected);
    };

    // Delete: confirm first, then remove the shown voice (never the last).
    layerPanel.getHeader().onDelete = [this]
    {
        if (proc.getNumVoices() <= 1) return;
        const juce::String name = proc.getChannelName(layerPanel.getLayer());
        mu_ui::confirmAsync(&layerPanel, "Delete Voice", "Delete \"" + name + "\"?\nThis cannot be undone.",
                            "Delete", [this] { deleteShownVoice(); });
    };

    // Forward mixer status updates to the shared StatusBar.

    sidebar.setSelectedIndex(0);
    layerPanel.setLayer(0);
    mixerOverlay.loadFromApvts();
    clearPresetDirty();

    // The family metal look, in mu-Tant's green.
    setMetalStyle(true, MuLookAndFeel::colour(MuLookAndFeel::appGreen));
    setScrews(true);

    // Every control on the main panel shows its name + value on the status bar.
    forwardKnobStatus(layerPanel);
}

PluginEditor::~PluginEditor()
{
    // The processor can outlive the editor (DAW close-window-keep-plugin); clear
    // the hot-swap callbacks so a boundary commit can't fire into a dead editor.
    proc.onLayerPresetCommitted = nullptr;
}

// Remove the shown voice. Everything that points into the voice's Pattern is unbound first (the
// edit moves the Pattern pointers and frees the removed one), so no timer or paint callback can
// dereference it in the window before setLayer rebinds.
void PluginEditor::deleteShownVoice()
{
    if (proc.getNumVoices() <= 1) return;
    const int idx = layerPanel.getLayer();
    layerPanel.getModulatorPanel().setVoiceSlot(nullptr);
    patternContent.clearAllModBindings();
    patternContent.clearPatternBindings();
    proc.removeLayer(idx);
    const int newIndex = juce::jlimit(0, proc.getNumVoices() - 1, idx);
    sidebar.refreshItems();
    sidebar.setSelectedIndex(newIndex);
    layerPanel.setLayer(newIndex);
}

void PluginEditor::onPresetLoaded(const juce::File&)
{
    sidebar.refreshItems();
    sidebar.setSelectedIndex(0);
    layerPanel.setLayer(0);         // re-reads insert algo + slot knobs from APVTS
    mixerOverlay.loadFromApvts();
}

void PluginEditor::onPresetNew()
{
    sidebar.refreshItems();
    sidebar.setSelectedIndex(0);
    layerPanel.setLayer(0);
    mixerOverlay.loadFromApvts();
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor(*this);
}

} // namespace mu_tant
