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
      // The content's modulation arcs hold a layer's matrix: release them before a removal frees it.
      layerEdit(p, sidebar, layerPanel, { [this] { releaseContentArcs(); }, [this](int idx) { layerPanel.setLayer(idx); } }),
      mixerOverlay(p, p.mixerEngine),
      settingsOverlay(p)
{
    setProductIdentity(juce::String(juce::CharPointer_UTF8("\xce\xbc-Tronic")));

    // A filled slot shows its type's own mini-graphic; an Empty slot shows a "+" (the sidebar builds none for it).
    sidebar.createMiniVisual = [this](int slot, std::function<void()> pulse) -> std::unique_ptr<juce::Component>
    {
        const auto* info = proc.getLayerTypes().find(proc.getLayerTypeId(slot));
        if (info == nullptr || info->createMiniVisual == nullptr) return nullptr;
        return info->createMiniVisual(proc, slot, std::move(pulse));
    };
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
    // old layer once it is retired. The shown content drops its modulation arcs and the modulator panel its layer; the
    // editor rebinds both when it shows the slot again.
    proc.onLayerTypeChanging = [this](int slot)
    {
        layerPanel.getModulatorPanel().setLayer(nullptr);
        juce::ignoreUnused(slot);
        releaseContentArcs();   // unconditional: cheap, and safe even if a content's binding differs from the shown slot
    };

    // A layer preset landed (a chosen file, a boundary commit): an Empty slot that took the file's type needs its
    // sidebar picture and panel rebuilt, a filled slot rebinds its shown content.
    proc.onLayerPresetCommitted = [this](int slot)
    {
        sidebar.refreshItems();
        mixerOverlay.loadFromApvts();
        if (layerPanel.getLayer() == slot) layerPanel.setLayer(slot);
    };

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
    layerPanel.setContent(nullptr);   // the panel must not keep a content this editor is about to destroy
    releaseContentArcs();
    layerPanel.getHeader().onNameChanged = nullptr;   // the processor can outlive the editor
    proc.onLayerTypeChanging = nullptr;
    proc.onLayerPresetCommitted = nullptr;
}

LayerContent* PluginEditor::contentFor(mu_core::LayerTypeId type)
{
    for (auto& entry : contents)
        if (entry.first == type) return entry.second.get();

    const auto* info = proc.getLayerTypes().find(type);
    if (info == nullptr || info->createContent == nullptr) return nullptr;
    auto content = info->createContent(proc, layerPanel.getLayer());
    // A knob hover or change shows on the shell's status bar.
    content->onStatusUpdate = [this](const juce::String& name, const juce::String& value) { getStatusBar().showParam(name, value); };
    contents.emplace_back(type, std::move(content));
    return contents.back().second.get();
}

void PluginEditor::releaseContentArcs()
{
    for (auto& entry : contents)
        entry.second->releaseModulationArcs();
}

void PluginEditor::showSlot(int idx)
{
    const auto type = proc.getLayerTypeId(idx);
    const bool empty = type == mu_core::LayerTypeId::Empty;
    layerPanel.setLayerEmpty(empty);
    layerPanel.getHeader().setNameEditable(! empty);
    chooser.setSlot(idx);

    // An Empty slot shows the chooser over the content and modulator bands, in the shell's own colour.
    if (empty)
    {
        shownContent = nullptr;
        MuLookAndFeel::clearAccentOverride(layerPanel);
        layerPanel.getHeader().setTypeTag({});
        layerPanel.getModulatorPanel().setDestProvider(nullptr);
        layerPanel.setContent(&chooser);
        return;
    }

    // A filled slot shows its type's content, one instance per type rebound to the slot, and the main area wears
    // the type's colour (the shell chrome keeps mu-Tronic's own).
    const auto* info = proc.getLayerTypes().find(type);
    shownContent = contentFor(type);
    if (shownContent != nullptr) shownContent->setLayer(idx);
    layerPanel.setContent(shownContent);
    layerPanel.getModulatorPanel().setDestProvider(shownContent != nullptr ? shownContent->getModDestProvider() : nullptr);
    layerPanel.getHeader().setTypeTag(info != nullptr ? juce::String(info->typeTag) : juce::String());
    if (info != nullptr && info->appColourId != 0)
        MuLookAndFeel::setAccentOverride(layerPanel, MuLookAndFeel::colour((MuLookAndFeel::ColourIds) info->appColourId));
    else
        MuLookAndFeel::clearAccentOverride(layerPanel);
    layerPanel.repaint();
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
