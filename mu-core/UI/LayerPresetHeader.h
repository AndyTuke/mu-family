#pragma once

#include "UI/LayerHeaderBar.h"
#include "UI/ConfirmDialog.h"
#include "Plugin/ProcessorBase.h"

// Wires a LayerHeaderBar's reset / preset dropdown / Save to the processor's per-slot preset
// API (ProcessorBase::resetLayer / loadLayerPreset / saveLayerPreset / slotPresetFiles), so every
// product's layer panel behaves the same: Reset asks first, a chosen preset loads (staged while
// playing) and shows as selected, Save prompts for a name and re-lists the slot's presets.
namespace mu_ui
{

struct SlotPresetHeaderConfig
{
    juce::String                     noun;          // "Voice" / "Layer" / "Track" — dialog titles
    std::function<int()>             currentSlot;   // the slot the panel shows
    std::function<void()>            resync;        // re-bind the panel after a reset / load
    std::function<juce::String(int)> defaultName;   // suggested save name; empty = the slot's name
};

// Fill the header's preset dropdown with the slot's presets.
inline void refreshSlotPresetList(LayerHeaderBar& header, const ProcessorBase& proc, int slot)
{
    header.setPresetFiles(proc.slotPresetFiles(slot));
}

inline void wireSlotPresetHeader(LayerHeaderBar& header, juce::Component& owner,
                                 ProcessorBase& proc, SlotPresetHeaderConfig cfg)
{
    auto* hdr = &header;
    auto* p   = &proc;
    juce::Component::SafePointer<juce::Component> safe(&owner);

    header.onReset = [safe, p, cfg]
    {
        if (safe == nullptr) return;
        const int slot = cfg.currentSlot();
        confirmAsync(safe.getComponent(), "Reset " + cfg.noun,
                     LayerHeaderBar::resetConfirmMessage(p->getLayerName(slot)),
                     "Reset", [safe, p, cfg, slot]
                     {
                         if (safe == nullptr) return;
                         p->resetLayer(slot);
                         cfg.resync();
                     });
    };

    header.onPresetFileChosen = [hdr, p, cfg](const juce::File& f)
    {
        p->loadLayerPreset(cfg.currentSlot(), f);
        cfg.resync();               // re-bind so the panel shows the loaded state
        hdr->showPresetFile(f);
    };

    header.onSave = [safe, hdr, p, cfg]
    {
        if (safe == nullptr) return;
        const int slot = cfg.currentSlot();
        promptTextAsync(safe.getComponent(), "Save " + cfg.noun + " Preset", "Preset name:",
                        cfg.defaultName ? cfg.defaultName(slot) : p->getLayerName(slot), "Save",
            [safe, hdr, p, slot](const juce::String& name)
            {
                if (safe == nullptr || name.isEmpty()) return;
                p->saveLayerPreset(slot, name);
                refreshSlotPresetList(*hdr, *p, slot);
            });
    };
}

} // namespace mu_ui
