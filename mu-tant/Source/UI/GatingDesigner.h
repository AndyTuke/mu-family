#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/Components/DropdownSelect.h"
#include "UI/Components/MuLookAndFeel.h"
#include "Sequencer/GatePattern.h"

namespace mu_tant
{

enum class GateTool  { Pencil, Eraser, Reverse };
enum class GateLayer { Gater, Filter, Pitch };

class GateToolButton : public juce::Button
{
public:
    explicit GateToolButton(GateTool t);
    GateTool tool() const noexcept { return toolId; }
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
private:
    GateTool toolId;
};

// Per-voice drawable gate editor.
//
// Layout (two bands, top to bottom):
//   Header row (kHdr1H): [Bypass][GATE][FILT][PITCH] | tools | Gap rotary | Bars dd | Grid dd
//   Grid        (kGridH): envelope drawing area — always shows kViewBars bars
//   Scrollbar   (kScrollH): smooth horizontal scroll through longer patterns
//
// Gap rotary and Bypass button live here; VoicePanel binds APVTS attachments
// via the public `gapSlider` and `bypassButton` members.
class GatingDesigner : public juce::Component,
                       private juce::ScrollBar::Listener
{
public:
    GatingDesigner();

    void setSubdivision(int denominator);
    int  getSubdivision() const noexcept { return subdivisionDenom; }

    void setPattern(GatePattern* pattern);
    void setFilterPattern(GatePattern* pattern);
    void setPitchPattern(GatePattern* pattern);
    void setGap(float gap01);
    void setLayer(GateLayer layer);
    void setPatternBars(int bars);
    GateLayer getLayer()     const noexcept { return currentLayer; }
    GateTool  selectedTool() const noexcept { return currentTool; }
    void setPlayhead(double beat01, bool visible);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

    static constexpr int kViewBars = 2;    // bars visible at once (public for tests)

    // Public: VoicePanel binds APVTS attachments to these.
    juce::Slider     gapSlider;
    juce::TextButton bypassButton { "Bypass" };

private:
    // ── Layer toggle ─────────────────────────────────────────────────────────
    juce::TextButton gaterLayerBtn  { "GATE" };
    juce::TextButton filterLayerBtn { "FILT" };
    juce::TextButton pitchLayerBtn  { "PITCH" };
    GateLayer currentLayer = GateLayer::Gater;

    // ── Subdivision ─────────────────────────────────────────────────────────
    juce::Label    subdivLabel;
    DropdownSelect subdivDropdown;

    // ── Pattern length (1-16 bars) ───────────────────────────────────────────
    DropdownSelect barsDropdown;

    // ── Toolbox (Pencil, Eraser, Reverse — no Arrow) ─────────────────────────
    GateToolButton pencilBtn  { GateTool::Pencil };
    GateToolButton eraserBtn  { GateTool::Eraser };
    GateToolButton reverseBtn { GateTool::Reverse };
    GateTool       currentTool = GateTool::Pencil;
    void selectTool(GateTool t);

    int   subdivisionDenom = 16;
    float gapValue         = 0.0f;

    GatePattern* boundPattern  = nullptr;
    GatePattern* filterPattern = nullptr;
    GatePattern* pitchPattern  = nullptr;

    // ── Layout constants ─────────────────────────────────────────────────────
    // Total: kHdr1H + kGridH + kScrollH + 4 = 186px (unchanged).
    static constexpr int kHdr1H    = 38;   // header row (holds size-3 rotary)
    static constexpr int kGridH    = 134;  // visual grid area
    static constexpr int kScrollH  = 10;   // horizontal scrollbar
    static constexpr int kHdrInset = 6;
    static constexpr int kDdW      = 88;   // Grid dropdown width
    static constexpr int kBarsW    = 52;   // Bars dropdown width (fits "16")
    static constexpr int kToolW    = 22;
    static constexpr int kToolGap  = 4;
    static constexpr int kBtnW     = 54;   // shared width: Bypass / GATE / FILT / PITCH
    static constexpr int kGapKnobW = 32;   // gap rotary diameter (size-3)
    static constexpr int kGapTbW   = 46;   // gap textbox width (fits "100 %")

    // ── Playhead ─────────────────────────────────────────────────────────────
    double playheadBeat01  = 0.0;
    bool   playheadVisible = false;

    // ── Scroll state ─────────────────────────────────────────────────────────
    double viewStartBar = 0.0;
    juce::ScrollBar scrollBar { false };

    void updateScrollRange();
    void setViewStart(double bar);

    void scrollBarMoved(juce::ScrollBar*, double newRangeStart) override
    {
        setViewStart(newRangeStart);
    }

    // ── Grid geometry ─────────────────────────────────────────────────────────
    int cellCount() const noexcept;
    juce::Rectangle<float> gridBounds() const noexcept;
    float viewCellW() const noexcept;
    int cellAtX(float x) const noexcept;

    GatePattern* getActivePattern() const noexcept
    {
        if (currentLayer == GateLayer::Gater)  return boundPattern;
        if (currentLayer == GateLayer::Filter) return filterPattern;
        return pitchPattern;
    }
    GatePattern* getGhostPattern() const noexcept
    {
        return (currentLayer == GateLayer::Gater) ? nullptr : boundPattern;
    }
    juce::Colour activeLayerColour() const noexcept;
    juce::Colour ghostLayerColour()  const noexcept;

    // ── Envelope handles (Pencil tool) ────────────────────────────────────────
    struct EnvLayout
    {
        float x0 = 0, wpx = 0, top = 0, bot = 0, h = 0, activeW = 0;
        juce::Point<float> peak, riseMid, fallMid;
    };
    EnvLayout layoutFor(const GateEnvelope& e) const noexcept;

    enum class Handle { None, Split, RiseBend, FallBend, StartGrab, EndGrab };
    struct HandleHit { int envIndex = -1; Handle handle = Handle::None; };
    HandleHit hitTestHandles(juce::Point<float> p, GatePattern* pat) const noexcept;

    enum class DragKind { None, Split, RiseBend, FallBend, StartEdge, EndEdge };
    DragKind dragKind        = DragKind::None;
    int      dragEnvIndex    = -1;
    float    dragStartBend   = 0.0f;
    float    dragStartY      = 0.0f;
    int      dragOriginalEnd = -1;

    // ── Path cache ───────────────────────────────────────────────────────────
    std::vector<juce::Path> activePathCache;
    std::vector<juce::Path> ghostPathCache;
    bool pathsDirty = true;
    void markPathsDirty() noexcept { pathsDirty = true; }
    void rebuildPathCache();

    template <typename Fn> void withLock(GatePattern* pat, Fn&& fn);

    static constexpr float kHandleRadius = 6.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GatingDesigner)
};

} // namespace mu_tant
