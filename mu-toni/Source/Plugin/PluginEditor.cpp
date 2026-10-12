#include "PluginEditor.h"
#include "UI/Components/MuLookAndFeel.h"

namespace mu_toni
{

PluginEditor::PluginEditor(PluginProcessor& p)
    : EditorShellBase(p),
      proc(p),
      sidebar(p, "Layer"),
      arpContent(p),
      layerPanel(p, "Layer"),
      mixerOverlay(p, p.mixerEngine),
      settingsOverlay(p)
{
    // Product chrome on the shared overlays.
    setProductIdentity(juce::String(juce::CharPointer_UTF8("\xce\xbc-Toni")));

    // Per-layer mini-graphic for the shared sidebar (placeholder until a real arp
    // visual is designed). Reorder/add stay unwired — the layer set is fixed.
    sidebar.createMiniVisual = [&p](int i, std::function<void()>) -> std::unique_ptr<juce::Component>
    {
        const auto col = MuLookAndFeel::channelPalette[
            (size_t) (p.getChannelColourIndex(i) % MuLookAndFeel::kChannelPaletteSize)];
        return std::make_unique<ArpMiniVisual>(col, i);
    };
    sidebar.onLayerSelected = [this](int idx) { layerPanel.setLayer(idx); };
    sidebar.refreshItems();

    // The shared layer panel: fixed layers, the arp content band in the middle, mu-Toni's
    // modulation destinations; a layer change rebinds the content's controls.
    layerPanel.setFixedLayers(true);
    layerPanel.setContent(&arpContent);
    layerPanel.setDestProvider(&modDestProvider);
    layerPanel.onLayerChanged = [this](int layer) { arpContent.setLayer(layer); };

    // Content-band knob hovers/changes → shared StatusBar.
    arpContent.onStatusUpdate = [this](const juce::String& name, const juce::String& val)
    {
        getStatusBar().showParam(name, val, MuLookAndFeel::colour(MuLookAndFeel::knobEuclidean));
    };

    // Main area (sidebar + blank engine panel) + shared mixer overlay.
    setMainArea(&sidebar, &layerPanel);
    setMixerOverlay(&mixerOverlay);

    // Settings page (master vol + UI size + BPM + standalone MIDI Clock) behind the
    // gear button — registering it reveals the gear (hidden when null).
    setSettingsOverlay(&settingsOverlay);

    // A program change loaded a layer preset → refresh that layer if it's on screen.
    proc.onLayerPresetCommitted = [this](int layer)
    {
        if (layerPanel.getLayer() == layer) layerPanel.setLayer(layer);
    };

    // Forward mixer status updates to the shared StatusBar.

    sidebar.setSelectedIndex(0);
    layerPanel.setLayer(0);
    mixerOverlay.loadFromApvts();
    clearPresetDirty();

    // The family metal look.
    setMetalStyle(true, MuLookAndFeel::colour(MuLookAndFeel::appBlue));
    setScrews(true);

    // Every control on the main panel shows its name + value on the status bar.
    forwardKnobStatus(layerPanel);
}

PluginEditor::~PluginEditor()
{
    proc.onLayerPresetCommitted = nullptr;   // the processor can outlive the editor
}

void PluginEditor::onPresetLoaded(const juce::File&)
{
    sidebar.refreshItems();
    layerPanel.setLayer(layerPanel.getLayer());   // rebind knobs + modulators to the loaded state
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

} // namespace mu_toni
