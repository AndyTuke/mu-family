#include "LayerEditController.h"
#include "UI/ConfirmDialog.h"

namespace mu_ui
{

LayerEditController::LayerEditController(ProcessorBase& processor, LayerSidebar& s, LayerPanel& p, Hooks h)
    : proc(processor), sidebar(s), panel(p), hooks(std::move(h))
{
    wire();
}

LayerEditController::~LayerEditController()
{
    // The sidebar and panel are owned by the editor and are destroyed after this; drop the
    // callbacks that capture this object.
    sidebar.onAddLayer = nullptr;
    sidebar.onLayersReordered = nullptr;
    panel.getHeader().onDelete = nullptr;
}

void LayerEditController::wire()
{
    // A fixed product leaves Add unwired, which hides the button (LayerSidebar::resized).
    sidebar.onAddLayer = fixed ? std::function<void()>{} : [this] { addLayer(); };
    sidebar.onLayersReordered = [this](int newSelected)
    {
        if (hooks.showLayer) hooks.showLayer(newSelected);
    };
    panel.getHeader().onDelete = [this] { confirmDelete(); };
}

void LayerEditController::setFixedLayers(bool f)
{
    fixed = f;
    panel.setFixedLayers(f);
    wire();
}

void LayerEditController::refreshAfterPresetLoad()
{
    sidebar.refreshItems();
    sidebar.setSelectedIndex(0);
    if (hooks.showLayer) hooks.showLayer(0);
}

// Append a layer and select it. A refused add (already at the maximum) does nothing; the sidebar
// disables its button at the maximum, so this is only a safety net.
void LayerEditController::addLayer()
{
    const int idx = proc.addLayer();
    if (idx < 0) return;
    sidebar.refreshItems();
    sidebar.setSelectedIndex(idx);
    if (hooks.showLayer) hooks.showLayer(idx);
}

// Ask first, then remove the shown layer. The header disables its delete at one layer; the check
// here keeps a stale click from reaching the processor.
void LayerEditController::confirmDelete()
{
    if (fixed || proc.getNumChannels() <= 1) return;
    const juce::String name = proc.getChannelName(panel.getLayer());
    confirmAsync(&panel, "Delete " + panel.getNoun(), "Delete \"" + name + "\"?\nThis cannot be undone.",
                 "Delete", [this] { deleteShownLayer(); });
}

// Remove the shown layer; the selection stays on the same index, clamped to the new last layer.
void LayerEditController::deleteShownLayer()
{
    if (proc.getNumChannels() <= 1) return;
    const int idx = panel.getLayer();

    // Everything that points into a layer's data is released first (see Hooks::unbindAll).
    panel.getModulatorPanel().setVoiceSlot(nullptr);
    if (hooks.unbindAll) hooks.unbindAll();

    proc.removeLayer(idx);
    const int newIndex = juce::jlimit(0, proc.getNumChannels() - 1, idx);
    sidebar.refreshItems();
    sidebar.setSelectedIndex(newIndex);
    if (hooks.showLayer) hooks.showLayer(newIndex);
}

} // namespace mu_ui
