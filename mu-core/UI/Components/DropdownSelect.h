#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>
#include "MuLookAndFeel.h"
#include "UI/StatusSink.h"

// Styled wrapper around juce::ComboBox.
class DropdownSelect : public juce::Component
{
public:
    std::function<void(int id)> onChange;

    DropdownSelect();

    void addItem(const juce::String& text, int id);
    void addSectionHeading(const juce::String& text);
    void setSelectedId(int id, bool notify = false);
    // The name the status bar shows with the chosen item when the user picks one ("Scale", "Filter Type"): the pick reports to the
    // nearest StatusSink above this control (the editor). Empty = this control reports nothing by itself.
    void setStatusName(const juce::String& name) { statusName = name; }
    const juce::String& getStatusName() const noexcept { return statusName; }
    int  getSelectedId() const;
    void clear();
    // An item whose popup lettering is `textColour` (e.g. a note outside the scale in red).
    void addItem(const juce::String& text, int id, juce::Colour textColour);
    void setPlaceholderText(const juce::String& text);

    // LCD look: dark glass with the value in glowing lettering (matches LCD tabs).
    void setLcdStyle(bool lcd);
    // Light the LCD in `c` instead of the app colour (backlit glass + lettering) — e.g. a
    // voice's own colour on its preset selector. Transparent = back to the app colour.
    void setLcdColour(juce::Colour c);

    // Shows `text` in place of the placeholder / selection, in the queued colour when `queued`. It survives
    // clear() and setSelectedId(0); choosing an item (setSelectedId(id > 0) or a pick) takes over. Empty text clears it.
    void setDisplayOverride(const juce::String& text, bool queued);
    bool hasDisplayOverride() const noexcept { return overrideText.isNotEmpty(); }
    bool isDisplayQueued() const noexcept { return overrideText.isNotEmpty() && overrideQueued; }

    // Returns the currently displayed item text (or empty if nothing selected).
    juce::String getText() const { return combo.getText(); }

    // Public accessor for binding to juce::AudioProcessorValueTreeState via
    // ComboBoxAttachment. Visual styling stays driven by DropdownSelect.
    juce::ComboBox& getComboBox() noexcept { return combo; }

    // ── Multi-select mode ───────────────────────────────────────────────────────────────────
    // Every item is a tick box: picking one toggles it and the popup opens again so several can be ticked in one visit. The
    // box shows the ticked items' texts joined by commas ("1, 2, 5"), "All" when every enabled item is ticked and "None" when
    // none is. Items are added with addItem as usual; the pick reports "<status name>: <summary>" to the status bar.
    void setMultiSelect(bool on);
    bool isMultiSelect() const noexcept { return multiSelect; }
    void setTicked(int id, bool ticked);          // no callback
    bool isTicked(int id) const;
    void setItemEnabled(int id, bool enabled);    // a disabled item cannot be picked and is not counted by "All"
    void toggle(int id);                          // what a pick does: flips the tick, refreshes the box, reports, calls onToggle
    std::function<void(int id, bool ticked)> onToggle;

    // ── Glyph mode ──────────────────────────────────────────────────────────────────────────
    // Each item is drawn with a small picture beside its name, in the popup and in the box (a curve, a shape): `draw` paints
    // the picture for item `id` in `area` in `colour`. Items are added with addItem as usual.
    using GlyphDrawer = std::function<void(juce::Graphics&, juce::Rectangle<float> area, int id, juce::Colour colour)>;
    void setItemGlyph(GlyphDrawer draw);

    void resized() override;
    void paintOverChildren(juce::Graphics& g) override;

private:
    class GlyphItem;
    void showGlyphPopup();
    GlyphDrawer glyphDrawer;
    // The ComboBox with its popup replaced in multi-select mode.
    class Combo : public juce::ComboBox
    {
    public:
        std::function<void()> popupOverride;
        void showPopup() override { if (popupOverride) popupOverride(); else juce::ComboBox::showPopup(); }
    };
    struct MultiItem { int id; juce::String text; bool ticked = false, enabled = true; };

    void applyOverride();
    void refreshTextColour();
    void showMultiPopup();
    void refreshMultiSummary();

    Combo combo;
    bool  multiSelect = false;
    std::vector<MultiItem> multiItems;
    juce::String   statusName;
    juce::String   overrideText;
    bool           overrideQueued = false;
};
