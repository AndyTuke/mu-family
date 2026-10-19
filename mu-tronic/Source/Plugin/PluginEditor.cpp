#include "PluginEditor.h"
#include "UI/Components/MuLookAndFeel.h"

namespace mu_tronic
{

PluginEditor::PluginEditor(PluginProcessor& p)
    : EditorShellBase(p),
      proc(p),
      sidebar(p, "Layer"),
      chooser(p.getLayerTypes()),
      layerPanel(p, "Layer"),
      // Nothing binds into a layer's data until a type brings its own content, so there is nothing to release yet.
      layerEdit(p, sidebar, layerPanel, { [] {}, [this](int idx) { layerPanel.setLayer(idx); } }),
      mixerOverlay(p, p.mixerEngine),
      settingsOverlay(p)
{
    setProductIdentity(juce::String(juce::CharPointer_UTF8("\xce\xbc-Tronic")));

    // A filled slot shows its type's own mini-graphic; an Empty slot shows a "+" (the sidebar builds none for it).
    // No type is registered yet, so no slot can be filled and this is never asked.
    sidebar.createMiniVisual = [](int, std::function<void()>) -> std::unique_ptr<juce::Component> { return nullptr; };
    sidebar.onLayerSelected = [this](int idx) { layerPanel.setLayer(idx); };
    sidebar.refreshItems();

    // Delete empties the slot (nothing shifts) and the layers are never added or reordered from the sidebar:
    // an Empty slot is the add affordance (the chooser).
    layerEdit.setFixedLayers(false);
    sidebar.onAddLayer = nullptr;

    // Names are editable and saved for every layer: a typed name stays, an emptied one returns to the default.
    layerPanel.getHeader().onNameChanged = [this](juce::String name)
    {
        proc.setLayerName(layerPanel.getLayer(), name);
        sidebar.repaintItems();
        mixerOverlay.refreshSidechainSources();
    };

    // Whoever changes a slot's type (the chooser, a state restore, a swap commit): nothing in the UI may point into the
    // old layer once it is retired.
    proc.onLayerTypeChanging = [this](int) { layerPanel.getModulatorPanel().setLayer(nullptr); };

    layerPanel.setDefaultPresetName([this](int slot) { return proc.getLayerName(slot); });
    layerPanel.onLayerChanged = [this](int idx) { showSlot(idx); };

    chooser.onChoose = [this](mu_core::LayerTypeId type)
    {
        const int slot = layerPanel.getLayer();
        layerPanel.getModulatorPanel().setLayer(nullptr);   // nothing may point into the layer being replaced
        if (proc.setLayerType(slot, type))
            layerTypeSet(slot);
    };

    setMainArea(&sidebar, &layerPanel);
    setMixerOverlay(&mixerOverlay);
    setSettingsOverlay(&settingsOverlay);

    sidebar.setSelectedIndex(0);
    layerPanel.setLayer(0);
    mixerOverlay.loadFromApvts();
    clearPresetDirty();

    // The family metal look, in mu-Tronic's own near-white.
    setMetalStyle(true, MuLookAndFeel::colour(MuLookAndFeel::appIce));
    setScrews(true);

    forwardKnobStatus(layerPanel);
}

PluginEditor::~PluginEditor()
{
    layerPanel.getHeader().onNameChanged = nullptr;   // the processor can outlive the editor
    proc.onLayerTypeChanging = nullptr;
}

void PluginEditor::showSlot(int idx)
{
    const bool empty = proc.getLayerTypeId(idx) == mu_core::LayerTypeId::Empty;
    layerPanel.setLayerEmpty(empty);
    layerPanel.getHeader().setNameEditable(! empty);
    // An Empty slot shows the chooser over the content and modulator bands; a filled slot's content comes with its type.
    layerPanel.setContent(empty ? &chooser : nullptr);
    chooser.setSlot(idx);
}

void PluginEditor::layerTypeSet(int slot)
{
    sidebar.refreshItems();
    sidebar.setSelectedIndex(slot);
    layerPanel.setLayer(slot);
    mixerOverlay.loadFromApvts();
    getStatusBar().showParam("Layer", "Added " + proc.getLayerName(slot));
}

void PluginEditor::onPresetLoaded(const juce::File&)
{
    layerEdit.refreshAfterPresetLoad();   // rebuild the sidebar (types may have changed), select slot 1, rebind the panel
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

} // namespace mu_tronic
