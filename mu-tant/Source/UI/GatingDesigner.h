#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/Components/DropdownSelect.h"
#include "UI/Components/MuLookAndFeel.h"
#include "Sequencer/GatePattern.h"

namespace mu_tant
{

// Edit tool the toolbox selects.
//   Pencil  — draw a 1-cell envelope; drag grab-handles to reshape one.
//   Eraser  — click an envelope to remove it.
//   Glue    — drag across envelopes to merge them into one wider region.
//   Reverse — click an envelope to flip its attack and decay.
enum class GateTool { Pencil, Eraser, Glue, Reverse };

// Small icon button that vector-draws one of the toolbox glyphs. Radio-grouped
// so exactly one tool is active. The icon is drawn procedurally (no asset
// dependency) so it scales with the UI and stays in the mu palette.
class GateToolButton : public juce::Button
{
public:
    explicit GateToolButton(GateTool t);
    GateTool tool() const noexcept { return toolId; }
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

private:
    GateTool toolId;
};

// Per-voice drawable gate editor: a full-width 2-bar grid of attack/decay
// envelopes. The note-length dropdown sets the cell subdivision; the toolbox
// picks the edit tool. See docs/mu-tant/design-sequencer.md.
class GatingDesigner : public juce::Component
{
public:
    GatingDesigner();

    // Set the subdivision note value. Affects the gridline density.
    // 4 = 1/4 → 8 cells, 8 = 1/8 → 16 cells, 16 = 1/16 → 32, 32 = 1/32 → 64.
    void setSubdivision(int denominator);
    int  getSubdivision() const noexcept { return subdivisionDenom; }

    // Bind to a per-voice GatePattern — edits write into it. Pass nullptr to
    // unbind. Ownership stays with the caller (PluginProcessor).
    void setPattern(GatePattern* pattern);

    // Per-voice Gap (0..1): trailing fraction of each region forced silent.
    // Used for rendering so the editor matches the audio gate. The value is
    // owned by APVTS (VoicePanel binds the knob); this is a render-only mirror.
    void setGap(float gap01);

    GateTool selectedTool() const noexcept { return currentTool; }

    // Move the playback timeline. `beat01` is the song position normalised to
    // 0..1 across the 2-bar grid; `visible` hides the line when stopped.
    void setPlayhead(double beat01, bool visible);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

    // Total bars represented by the strip. Fixed at 2 per the design spec.
    static constexpr int kTotalBars = 2;

private:
    juce::Label    subdivLabel;
    DropdownSelect subdivDropdown;

    // Toolbox: pencil / eraser / glue / reverse.
    GateToolButton pencilBtn  { GateTool::Pencil };
    GateToolButton eraserBtn  { GateTool::Eraser };
    GateToolButton glueBtn    { GateTool::Glue };
    GateToolButton reverseBtn { GateTool::Reverse };
    GateTool       currentTool = GateTool::Pencil;
    void selectTool(GateTool t);

    int subdivisionDenom = 16;   // 1/16 default — 32 cells over 2 bars
    float gapValue = 0.0f;       // render-only mirror of the per-voice Gap param

    GatePattern* boundPattern = nullptr;

    static constexpr int kHeaderH      = 24;   // header band height
    static constexpr int kGridH        = 80;   // gate rectangle height
    static constexpr int kHeaderInset  = 6;
    static constexpr int kDropdownW    = 88;
    static constexpr int kToolW        = 22;   // toolbox button size
    static constexpr int kToolGap      = 4;

    // Playback timeline.
    double playheadBeat01 = 0.0;
    bool   playheadVisible = false;

    int cellCount() const noexcept;
    // The gate rectangle (below the header), in component coordinates.
    juce::Rectangle<float> gridBounds() const noexcept;
    // Cell under an x coordinate (clamped into range), or -1 if outside the grid.
    int cellAtX(float x) const noexcept;

    // ── Grab-handle interaction (Pencil tool) ────────────────────────────────
    // Pixel geometry of one envelope's curve + the three drag handles.
    struct EnvLayout
    {
        float x0 = 0, wpx = 0, top = 0, bot = 0, h = 0, activeW = 0;
        juce::Point<float> peak, riseMid, fallMid;   // split / attack / decay handles
    };
    EnvLayout layoutFor(const GateEnvelope& e) const noexcept;

    enum class Handle { None, Split, RiseBend, FallBend };
    struct HandleHit { int envIndex = -1; Handle handle = Handle::None; };
    HandleHit hitTestHandles(juce::Point<float> p) const noexcept;

    enum class DragKind { None, Split, RiseBend, FallBend, GlueRange };
    DragKind dragKind     = DragKind::None;
    int      dragEnvIndex = -1;
    float    dragStartBend = 0.0f;
    float    dragStartY    = 0.0f;
    int      glueFirstCell = -1;
    int      glueLastCell  = -1;

    // Run `fn` under the bound pattern's editLock (brief UI-side hold).
    template <typename Fn> void withPatternLock(Fn&& fn);

    static constexpr float kHandleRadius = 6.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GatingDesigner)
};

} // namespace mu_tant
