#pragma once
#include "UI/MasterInsertPanel.h"

class ProcessorBase;

namespace mu_ui
{
// The plugin half of the master insert column: wires a callback-only MasterInsertPanel to the
// processor's mst_ins* / mst_ins2* parameters. User changes in the panel are written with
// setValueNotifyingHost; refresh() pushes the engine's current insert state back into the panel.
// It takes ProcessorBase, so it stays out of the shared chrome source list (like MixerOverlay).
// The panel must outlive the binding and be used from the message thread only.
class MasterInsertBinding
{
public:
    MasterInsertBinding(ProcessorBase& processor, MasterInsertPanel& insertPanel);
    ~MasterInsertBinding();

    // Panel from the engine's insert state (message thread; never from the audio thread). Reads the
    // algorithm and the four parameters of both slots from mixerEngine.getMasterInsertParams.
    void refresh();

    // The APVTS ids behind a slot (0 = Insert 1, 1 = Insert 2).
    static const char* algorithmParamId(int slot) noexcept;
    static const char* paramId(int slot, int param) noexcept;

private:
    void write(const char* parameterId, float plainValue);

    ProcessorBase&     proc;
    MasterInsertPanel& panel;
};
} // namespace mu_ui
