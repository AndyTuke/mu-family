#pragma once
#include <deque>

#include "UI/EditorShellBase.h"
#include "UI/LayerSidebar.h"          // mu-core shared sidebar (select / delete / pages)
#include "UI/LayerEditController.h"   // mu-core: delete glue
#include "UI/MixerOverlay.h"          // mu-core shared mixer
#include "UI/LayerPanel.h"            // mu-core shared per-layer panel shell
#include "UI/LayerContent.h"          // mu-core: a layer type's content component
#include "Plugin/PluginProcessor.h"
#include "UI/EmptySlotChooser.h"
#include "UI/TronicSettingsOverlay.h"

namespace mu_tronic
{

// mu-Tronic editor: the shared mu-core shell (TransportBar / StatusBar / About / overlays / window sizing /
// MuLookAndFeel) + the shared LayerSidebar (paged, eight slots at a time) + the shared MixerOverlay (its strips paged
// the same way), around the shared LayerPanel (preset strip + modulators). An Empty slot shows the chooser in the
// content area; a slot that holds a layer shows that type's content, built through the registry (one instance per
// type, rebound as the selection moves) and painted in the type's colour. No bespoke shell or mixer code.
// How a product built on this shell asks for it (mu-Host: one slot, no sidebar, no scenes, no clips, no mixer, no full-preset controls).
struct EditorOptions
{
    bool singleSlot = false;
};

class PluginEditor : public EditorShellBase,
                     private juce::Timer
{
public:
    explicit PluginEditor(PluginProcessor&, const EditorOptions& options = EditorOptions());
    ~PluginEditor() override;

protected:
    // Shell hooks: after a full preset loads (or New), re-read every panel from the state.
    void onPresetLoaded(const juce::File& file) override;
    void onPresetNew() override;

    // For a product built on this shell: called whenever a slot is shown (a selection, a type change, a restore), before the
    // main area is set up for it. Not called to a derived class while this one is still being constructed.
    virtual void slotShown(int slot, mu_core::LayerTypeId type) { juce::ignoreUnused(slot, type); }
    // Gives the slot being shown the chosen type, exactly as the chooser's cards do.
    void chooseType(mu_core::LayerTypeId type) { if (chooser.onChoose) chooser.onChoose(type); }
    // For a product built on this shell: the Empty-slot chooser (mu-Host limits its cards to licensed engines).
    EmptySlotChooser& getChooser() noexcept { return chooser; }
    PluginProcessor& proc;
    const bool singleSlot;

private:
    // Shows slot `idx` in the main area: the chooser while it is Empty, else the type's content.
    void showSlot(int idx);
    // The content instance for `type`, built on first use through the registry (null for a type with none).
    LayerContent* contentFor(mu_core::LayerTypeId type);
    // The destination provider for the shown slot: the content's own when it has one (mu-Clid's, mu-Tant's), else one built from the
    // targets its layer type publishes in the registry.
    const ModDestProvider* destProviderFor(mu_core::LayerTypeId type, const LayerContent* content);
    // Every content lets go of its modulation arcs (before a layer is replaced or removed).
    void releaseContentArcs();
    // The slot's layer was given a type: rebuild what depends on it.
    void layerTypeSet(int slot);
    // The mixer's effect name reaches the shown content's send label and its modulation targets.
    void applyEffectSendName();
    // The shown slot's MIDI-in channel (the header's Notes In control reads and writes ch{N}_midiIn).
    int  notesInChannel() const;
    void setNotesInChannel(int channel);
    // The preset menu's "Import product preset..." entry: asks for a product's full preset and lays its layers into the
    // Empty slots (PluginProcessor::importProductPreset); the status bar reports the outcome either way.
    void chooseProductPreset();
    void importProductPreset(const juce::File& file);
    // The status bar's CPU readout, refreshed from the processor's peak four times a second.
    void timerCallback() override;

    LayerSidebar   sidebar;
    EmptySlotChooser chooser;
    LayerPanel       layerPanel;
    mu_ui::LayerEditController layerEdit;   // delete glue; after the sidebar and panel
    MixerOverlay     mixerOverlay;
    TronicSettingsOverlay settingsOverlay;
    std::vector<std::pair<mu_core::LayerTypeId, std::unique_ptr<LayerContent>>> contents;   // one per type shown so far
    std::deque<std::pair<mu_core::LayerTypeId, ModDestProvider>> registryProviders;          // built from a type's published targets, once per type (a deque: the panel holds the pointers)
    LayerContent* shownContent = nullptr;
    juce::String  effectSendName;   // the mixer effect's current name; empty until the mixer reports it
    std::unique_ptr<juce::FileChooser> importChooser;   // alive while the import file dialog is open

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};

} // namespace mu_tronic
