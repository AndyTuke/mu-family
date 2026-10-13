#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/Components/MuLookAndFeel.h"

namespace mu_ui
{

// Shared chrome + layout framework for product settings overlays (mu-clid, mu-tant, …).
// Owns the top "Settings" header bar, the Close button, the centred & width-capped
// content column, and the group/section header drawing — so every product's settings
// page shares one look. Subclasses add their own controls, position them in
// layoutContent() (within contentX()/contentW(), starting at contentTop()), and draw
// their section headers / hints in paintContent() via the protected draw* helpers.
class SettingsOverlayBase : public juce::Component
{
public:
    std::function<void()> onClose;

    SettingsOverlayBase();
    ~SettingsOverlayBase() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

protected:
    // ── Subclass hooks ────────────────────────────────────────────────────────
    // Position child controls. The base has already laid out the Close button and
    // computed the content column, so contentX()/contentW()/contentTop() are valid.
    virtual void layoutContent() {}
    // Draw section headers / hints. The base has already filled the background and
    // painted the header bar + divider.
    virtual void paintContent(juce::Graphics&) {}

    // ── Content-column geometry (valid inside layoutContent()/paintContent()) ───
    int contentX()   const noexcept { return colX; }
    int contentW()   const noexcept { return colW; }
    int contentTop() const noexcept;   // first usable Y, below the header bar + page pad

    // Row geometry — controls sit left-aligned, indented from the section headings
    // (which start at contentX()). Shared so every product's rows line up the same.
    int rowLabelX()   const noexcept { return colX + s(kRowIndent); }
    int rowControlX() const noexcept { return colX + s(kRowIndent) + s(kLabelW) + s(kLabelCtrlGap); }

    // Two-column page: the content area splits into two equal columns when the overlay is wide
    // enough (otherwise one), so every control fits without scrolling. columnX/columnW give
    // column i's geometry; with one column both are the single content column.
    int columnCount() const noexcept { return cols; }
    int columnX(int i) const noexcept { return cols == 2 ? twoColX + i * (twoColW + s(kColumnGap)) : colX; }
    int columnW()      const noexcept { return cols == 2 ? twoColW : colW; }

    // ── Styled drawing helpers for paintContent() ──────────────────────────────
    // Group header — larger font + thick full-width divider (sub-panel banner).
    void drawGroupHeader  (juce::Graphics&, int y, const juce::String& title, int x = -1, int w = -1) const;
    // Section sub-header — smaller font + thin divider (a section within a group).
    void drawSectionHeader(juce::Graphics&, int y, const juce::String& title, int x = -1, int w = -1) const;

public:   // a product section's paint callback uses the hint helper and the standard row metrics
    // Secondary annotation drawn vertically centred on yCentre, within [x, x+w).
    void drawHint         (juce::Graphics&, int yCentre, const juce::String& text,
                           int x, int w) const;
    static constexpr int kRowH   = 26;
    static constexpr int kRowGap = 8;
protected:

    // ── Shared layout constants (unscaled; pass through mu_ui::s) ──────────────
    static constexpr int kHeaderH      = 44;   // top "Settings" bar
    static constexpr int kPad          = 20;   // page padding
    static constexpr int kGroupHeadH   = 30;   // sub-panel group header band
    static constexpr int kGroupGap     = 16;   // vertical gap between sub-panels
    static constexpr int kSectionGap   = 10;   // vertical gap between sections within a sub-panel
    static constexpr int kSectionHeadH = 22;   // section header band height
    static constexpr int kRowIndent    = 16;   // rows indented this far in from the section headings
    static constexpr int kLabelW       = 90;   // left-aligned row label column
    static constexpr int kControlW     = 220;
    static constexpr int kContentMaxW  = 620;  // cap content column so it doesn't sprawl
    static constexpr int kLabelCtrlGap = 12;   // horizontal gap between label and control
    static constexpr int kColumnGap    = 32;   // gap between the two page columns
    static constexpr int kTwoColMinW   = 800;  // overlay narrower than this stays single-column
    static constexpr int kTwoColMaxW   = 1000; // total width of both columns together
    static constexpr int kCloseBtnW    = 70;
    static constexpr int kCloseBtnH    = 26;

private:
    void computeColumn();   // populates colX/colW from the current width

    juce::TextButton closeBtn { "Close" };
    juce::HyperlinkButton supportLink;   // "Contact Support" → mailto:support@transwarp.me (footer)
    int colX = 0;
    int colW = 0;
    int cols = 1;
    int twoColX = 0;
    int twoColW = 0;
};

} // namespace mu_ui
