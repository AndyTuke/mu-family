#include "Plugin/PluginEditor.h"

#include "UI/Components/LayerDiscVisual.h"   // mu-core: the sidebar disc
#include "UI/Components/MuLookAndFeel.h"

namespace mu_tilate
{

PluginEditor::PluginEditor(PluginProcessor& p)
    : EditorShellBase(p),
      proc(p),
      sidebar(p, "Layer"),
      content(p, p),
      layerPanel(p, "Layer"),
      // The knobs' live modulation arcs hold a layer's matrix: release them before a removal frees it.
      layerEdit(p, sidebar, layerPanel, { [this] { content.releaseModulationArcs(); },
                                          [this](int idx) { layerPanel.setLayer(idx); } }),
      mixerOverlay(p, p.mixerEngine),
      settingsOverlay(p)
{
    setProductIdentity(juce::String(juce::CharPointer_UTF8("\xce\xbc-Tilate")));

    sidebar.createMiniVisual = [&p](int i, std::function<void()>) -> std::unique_ptr<juce::Component>
    {
        const auto col = MuLookAndFeel::layerPalette[(size_t) (p.getLayerColourIndex(i) % MuLookAndFeel::kLayerPaletteSize)];
        return std::make_unique<mu_ui::LayerDiscVisual>(col, juce::String(i + 1));
    };
    sidebar.onLayerSelected = [this](int idx) { layerPanel.setLayer(idx); };
    sidebar.refreshItems();

    // The shared layer panel: the content band in the middle, mu-Tilate's modulation destinations; a layer change
    // rebinds the content's controls. The layer count is variable (1 to 8).
    layerEdit.setFixedLayers(false);
    layerPanel.getHeader().setNameEditable(false);   // a layer is named after its index
    layerPanel.setContent(&content);
    layerPanel.setDestProvider(&modDestProvider);
    layerPanel.onLayerChanged = [this](int layer) { content.setLayer(layer); };

    content.onInsertAlgorithmChanged = [this](int algo) { layerPanel.getModulatorPanel().setInsertAlgorithm(algo); };
    content.onStatusUpdate = [this](const juce::String& name, const juce::String& val)
    {
        getStatusBar().showParam(name, val, MuLookAndFeel::colour(MuLookAndFeel::knobEuclidean));
    };

    setMainArea(&sidebar, &layerPanel);
    setMixerOverlay(&mixerOverlay);
    setSettingsOverlay(&settingsOverlay);

    // A program change loaded a layer preset: refresh that layer if it is on screen.
    proc.onLayerPresetCommitted = [this](int layer)
    {
        if (layerPanel.getLayer() == layer) layerPanel.setLayer(layer);
    };

    sidebar.setSelectedIndex(0);
    layerPanel.setLayer(0);
    mixerOverlay.loadFromApvts();
    clearPresetDirty();

    // The family metal look.
    setMetalStyle(true, MuLookAndFeel::colour(MuLookAndFeel::appBlue));
    setScrews(true);
    forwardKnobStatus(layerPanel);
}

PluginEditor::~PluginEditor()
{
    proc.onLayerPresetCommitted = nullptr;   // the processor can outlive the editor
}

void PluginEditor::onPresetLoaded(const juce::File&)
{
    layerEdit.refreshAfterPresetLoad();   // rebuild the sidebar (the count may have changed), rebind knobs + modulators
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

} // namespace mu_tilate
