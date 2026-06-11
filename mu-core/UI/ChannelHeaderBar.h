#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/Components/DropdownSelect.h"
#include "UI/Components/MuLookAndFeel.h"

// Shared per-layer header bar for every mu-product — identical across the family.
// Left: a colour dot + editable layer name. Right: reset (↺), delete (✕), a
// per-layer preset dropdown, and Save. The bar owns only the widgets + layout +
// the colour-dot/name-box chrome; the product wires the callbacks to its own
// reset / delete / preset-load / save / rename semantics, so behaviour
// (hot-swap staging in mu-clid, voice-preset I/O in mu-tant, etc.) stays product
// side while the look + UX are one shared implementation.
class ChannelHeaderBar : public juce::Component
{
public:
    ChannelHeaderBar();

    void setLayerName(const juce::String& n);
    void setColour(juce::Colour c);
    // Populate the preset dropdown. `items` may contain section headings via the
    // DropdownSelect API caller; here we take a flat name list + the file-index
    // mapping is the product's concern (onPresetSelected gives the dropdown id).
    void setPresetItems(const juce::StringArray& names);
    void setSelectedPresetId(int id);                 // 1-based dropdown id, 0 = none
    void setPresetPlaceholder(const juce::String& t);
    void setStagingBadge(bool show);                  // optional hot-swap "SWP" pill
    void setShowReset(bool show);                     // some products may hide reset
    void setSaveEnabled(bool enabled);                // demo: per-layer save disabled when unlicensed
    void commitNameEdit();                            // commit any in-progress rename (call before switching layer)

    DropdownSelect& getPresetDropdown() noexcept { return presetDD; }

    std::function<void()>             onReset;
    std::function<void()>             onDelete;
    std::function<void()>             onSave;
    std::function<void(int id)>       onPresetSelected;   // 1-based dropdown id
    std::function<void(juce::String)> onNameChanged;      // committed name edit

    void resized() override;
    void paint(juce::Graphics&) override;

    static constexpr int kHeight = 28;

private:
    juce::Label      nameLabel;
    juce::TextButton resetBtn  { juce::String::charToString(0x21BA) };  // ↺
    juce::TextButton deleteBtn { juce::String::charToString(0x2715) };  // ✕
    DropdownSelect   presetDD;
    juce::TextButton saveBtn   { "Save" };

    juce::Colour colour { juce::Colours::grey };
    bool         staging   = false;
    bool         showReset = true;

    static constexpr int kIconBtnW   = 22;
    static constexpr int kPresetBtnW = 38;
    static constexpr int kDotX       = 10;
    static constexpr int kNameX      = 26;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelHeaderBar)
};
