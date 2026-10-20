#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/ClipStrip.h"
#include "UI/Components/DropdownSelect.h"
#include "UI/Components/MuLookAndFeel.h"

// Shared per-layer header bar for every mu-product — identical across the family.
// Left: a colour dot + editable layer name. Right: reset (↺), delete (✕), a
// per-layer preset dropdown, and Save. The bar owns only the widgets + layout +
// the colour-dot/name-box chrome; the product wires the callbacks to its own
// reset / delete / preset-load / save / rename semantics, so behaviour
// (hot-swap staging in mu-clid, voice-preset I/O in mu-tant, etc.) stays product
// side while the look + UX are one shared implementation.
class LayerHeaderBar : public juce::Component
{
public:
    LayerHeaderBar();

    // The Reset confirm's message, one wording for every product: a reset also returns the layer's
    // mixer strip (level, pan, sends) to defaults, so the dialog says so, and empties its clip bank,
    // which it says only when there is something to lose.
    static juce::String resetConfirmMessage(const juce::String& layerName, int clipCount = 0)
    {
        return "Reset \"" + layerName + "\" to defaults?\nThis also resets its mixer strip."
             + (clipCount > 0 ? juce::String("\nThis also clears its clips.") : juce::String())
             + "\nThis cannot be undone.";
    }

    void setLayerName(const juce::String& n);
    void setColour(juce::Colour c);
    // Populate the preset dropdown. `items` may contain section headings via the
    // DropdownSelect API caller; here we take a flat name list + the file-index
    // mapping is the product's concern (onPresetSelected gives the dropdown id).
    void setPresetItems(const juce::StringArray& names);
    // Or populate it from preset files (shown without extension); onPresetFileChosen then
    // hands back the chosen file, so the product needn't keep its own index → file list.
    void setPresetFiles(const juce::Array<juce::File>& files);
    void showPresetFile(const juce::File& file);      // show `file` as the selected preset (no callback)
    void setSelectedPresetId(int id);                 // 1-based dropdown id, 0 = none
    void setPresetPlaceholder(const juce::String& t);
    void setStagingBadge(bool show);                  // optional hot-swap "SWP" pill
    void setShowReset(bool show);                     // some products may hide reset
    void setShowDelete(bool show);                    // products with fixed layers hide delete
    void setShowSave(bool show);                      // a slot host hides Save on an empty slot
    void setGhostName(bool ghost);                    // the name LCD unlit (an empty slot reads "Slot 5" in ghost lettering)
    void setDeleteEnabled(bool enabled);              // greyed when only one layer remains
    void setNoun(const juce::String& n);              // the product's word for a layer (tooltips); lower-cased
    void setNameEditable(bool editable);              // fixed-name layers can't be renamed
    // A small raised name plate between the colour dot and the name LCD naming the layer's type ("Toni", "On").
    // Off by default; empty hides it. A host of several layer types sets it per shown layer.
    void setTypeTag(const juce::String& text);
    // The layer's clip strip, between the preset dropdown and Save. Off by default: a product with no clip bank
    // (no hot-swap, so nothing to launch into) never shows it. It adds no height.
    void setShowClips(bool show);
    bool getShowClips() const noexcept { return showClips; }
    void bindClips(const mu_core::LiveView* view, mu_core::ControlSink* sink) { clipStrip.bind(view, sink); }
    void setClipLayer(int layerIndex)                 { clipStrip.setLayer(layerIndex); }
    ClipStrip& getClipStrip() noexcept                { return clipStrip; }
    void commitNameEdit();                            // commit any in-progress rename (call before switching layer)

    DropdownSelect& getPresetDropdown() noexcept { return presetDD; }

    std::function<void()>             onReset;
    std::function<void()>             onDelete;
    std::function<void()>             onSave;
    std::function<void(int id)>       onPresetSelected;   // 1-based dropdown id
    std::function<void(const juce::File&)> onPresetFileChosen;   // with setPresetFiles
    std::function<void(juce::String)> onNameChanged;      // committed name edit

    void resized() override;
    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void lookAndFeelChanged() override;

    static constexpr int kHeight = 28;

private:
    juce::Label      nameLabel;
    juce::TextButton resetBtn  { juce::String::charToString(0x21BA) };  // ↺
    juce::TextButton deleteBtn { juce::String::charToString(0x2715) };  // ✕
    DropdownSelect   presetDD;
    ClipStrip        clipStrip;
    juce::TextButton saveBtn   { "Save" };

    juce::Rectangle<float> nameDisplayBounds() const;   // the name's LCD (metal style)

    juce::Array<juce::File> presetFiles;   // set by setPresetFiles
    juce::Colour colour { juce::Colours::grey };
    bool         staging   = false;
    bool         showReset = true;
    bool         showDelete = true;
    bool         showSave   = true;
    bool         ghostName  = false;
    bool         showClips  = false;
    juce::String noun { "layer" };
    juce::String typeTag;

    int typeTagWidth() const;   // the plate and the gap after it, scaled; 0 with no tag

    static constexpr int kIconBtnW   = 22;
    static constexpr int kPresetBtnW = 38;
    static constexpr int kDotX       = 10;
    static constexpr int kNameX      = 26;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LayerHeaderBar)
};
