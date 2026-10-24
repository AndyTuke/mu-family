#pragma once
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include "Plugin/ProcessorBase.h"
#include "UI/LayerSidebar.h"
#include "UI/LayerPanel.h"

namespace mu_ui
{

// The editor glue for adding, deleting and reordering layers, shared by every product with
// variable layers (design-ui-family.md "Layer add / delete / reorder"). It wires the sidebar's Add
// and reorder callbacks and the layer header's delete (with its confirmation) to the processor's
// structural edits, and keeps the selection right afterwards. The product supplies only the two
// things that are its own: how to let go of everything bound to a layer's data, and how to show a
// layer. The editor owns the controller and must declare it after the sidebar and the panel.
class LayerEditController
{
public:
    struct Hooks
    {
        // Release every control and attachment bound into the layers' data. Called just before a
        // layer is removed, because the removal moves the layer objects and frees one, and no timer
        // or paint callback may reach them before showLayer rebinds.
        std::function<void()> unbindAll;
        // Show layer `index` (normally LayerPanel::setLayer plus any product rebind).
        std::function<void(int)> showLayer;
        // Optional: asked before a layer is added (a product that wants to know what kind first, such as mu-On's engine menu).
        // The product calls `add` when the user has chosen, and not at all if they cancel. Unset: the layer is added at once.
        std::function<void(std::function<void()> add)> beforeAdd;
    };

    LayerEditController(ProcessorBase& processor, LayerSidebar& sidebar, LayerPanel& panel, Hooks hooks);
    ~LayerEditController();

    // A product whose layer set is fixed: no delete, no rename, no Add button.
    void setFixedLayers(bool fixed);

    // After a preset load or New: rebuild the sidebar, select layer 0, rebind the panel, in that order.
    void refreshAfterPresetLoad();

private:
    void addLayer();
    void addLayerNow();
    void confirmDelete();
    void deleteShownLayer();
    void wire();

    ProcessorBase& proc;
    LayerSidebar&  sidebar;
    LayerPanel&    panel;
    Hooks          hooks;
    bool           fixed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LayerEditController)
};

} // namespace mu_ui
