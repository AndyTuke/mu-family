#include "PluginEditor.h"
#include "UI/Components/MuLookAndFeel.h"

namespace mu_on
{

PluginEditor::PluginEditor(PluginProcessor& p)
    : EditorShellBase(p),
      proc(p),
      sidebar(p, "Track"),
      trackContent(p),
      layerPanel(p, "Track"),
      // The content binds only APVTS parameters and reads tracks by index, never holding a pointer into
      // a layer's data, so there is nothing to release before a removal.
      layerEdit(p, sidebar, layerPanel, { {}, [this](int idx) { showTrack(idx); } }),
      mixerOverlay(p, p.mixerEngine),
      settingsOverlay(p)
{
    setProductIdentity(juce::String(juce::CharPointer_UTF8("\xce\xbc-On")));

    for (int e = 0; e < kNumEngineTypes; ++e)
        modProviders[(size_t) e] = makeModDestProvider(engineFromIndex(e));

    // Per-track sidebar glyph (the engine's initial in the track colour).
    sidebar.createMiniVisual = [&p](int i, std::function<void()>) -> std::unique_ptr<juce::Component>
    {
        const auto col = MuLookAndFeel::layerPalette[
            (size_t) (p.getLayerColourIndex(i) % MuLookAndFeel::kLayerPaletteSize)];
        const juce::String name = p.getLayerName(i);
        return std::make_unique<TrackMiniVisual>(col, name.substring(0, 1));
    };
    sidebar.onLayerSelected = [this](int idx) { showTrack(idx); };
    // Pulse a track in the sidebar when the sequencer fires it (polled ~5 Hz; the step rows' own
    // 30 Hz playhead is the primary timing feedback).
    sidebar.onAnimationTick = [this]
    {
        for (int ch = 0; ch < proc.getNumLayers(); ++ch)
        {
            const int c = proc.triggerCount(ch);
            if (c != lastTriggers[(size_t) ch]) { lastTriggers[(size_t) ch] = c; sidebar.pulseItem(ch); }
        }
    };
    sidebar.refreshItems();

    // The shared layer panel: the track content in the middle, the modulation destinations of the shown
    // track's engine. The track count is variable (1 to 8): the controller wires the sidebar Add button,
    // reorder and the header's delete. A track is named after its engine, so the header's name is not
    // editable.
    layerEdit.setFixedLayers(false);
    layerPanel.getHeader().setNameEditable(false);
    layerPanel.setContent(&trackContent);
    layerPanel.onLayerChanged = [this](int layer)
    {
        trackContent.setLayer(layer);
        useDestinationsOf(layer);
    };
    trackContent.onRowClicked = [this](int row) { showTrack(row); };
    trackContent.onEngineChanged = [this] { engineChanged(); };

    setMainArea(&sidebar, &layerPanel);
    setMixerOverlay(&mixerOverlay);

    // Settings page (master vol + UI size + BPM + standalone MIDI Clock) behind the
    // gear button — registering it reveals the gear (hidden when null).
    settingsOverlay.addProgramChangeSection("Track Presets", "Full Presets");   // Ch 1-8 → tracks, Ch 9 → full
    setSettingsOverlay(&settingsOverlay);

    // A program change loaded a track preset (which can carry another engine) → refresh it if it's on screen.
    proc.onLayerPresetCommitted = [this](int track)
    {
        sidebar.refreshItems();
        if (layerPanel.getLayer() == track) layerPanel.setLayer(track);
    };

    sidebar.setSelectedIndex(0);
    layerPanel.setLayer(0);
    mixerOverlay.loadFromApvts();
    clearPresetDirty();

    // The family metal look.
    setMetalStyle(true, MuLookAndFeel::colour(MuLookAndFeel::appYellow));
    setScrews(true);

    // Every control on the main panel shows its name + value on the status bar.
    forwardKnobStatus(layerPanel);
}

PluginEditor::~PluginEditor()
{
    proc.onLayerPresetCommitted = nullptr;   // the processor can outlive the editor
}

void PluginEditor::showTrack(int idx)
{
    sidebar.setSelectedIndex(idx);
    layerPanel.setLayer(idx);
}

// The shown track runs another engine: the sidebar items (named after the engine), the header and the
// modulator panel (its destinations are the new engine's) all follow.
void PluginEditor::engineChanged()
{
    sidebar.refreshItems();
    const int idx = layerPanel.getLayer();
    useDestinationsOf(idx);
    layerPanel.refreshHeader();
    layerPanel.getModulatorPanel().setLayer(proc.getLayer(idx));   // rebind: list the new engine's assignments
}

void PluginEditor::useDestinationsOf(int idx)
{
    layerPanel.setDestProvider(&modProviders[(size_t) toIndex(proc.engineOf(idx))]);
}

void PluginEditor::onPresetLoaded(const juce::File&)
{
    layerEdit.refreshAfterPresetLoad();   // rebuild the sidebar (the count may have changed), rebind content + modulators
    mixerOverlay.loadFromApvts();
}

void PluginEditor::onPresetNew()
{
    onPresetLoaded({});
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor(*this);
}

} // namespace mu_on
