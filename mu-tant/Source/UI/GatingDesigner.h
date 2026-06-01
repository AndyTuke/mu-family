#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/Components/DropdownSelect.h"
#include "UI/Components/MuLookAndFeel.h"
#include "UI/Components/KnobWithLabel.h"
#include "Sequencer/GatePattern.h"

namespace mu_tant
{

enum class GateTool  { Arrow, Pencil, Eraser, Reverse };
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
// Layout (three bands, top to bottom):
//   Header row 1 (kHdr1H): layer toggle | toolbox | Bypass btn | Grid dropdown
//   Header row 2 (kHdr2H): Gap: ========slider======== [value]
//   Grid           (kGridH): 2-bar envelope grid
//   Properties     (kPropsH): prob knob + loop-mask buttons (Arrow tool)
//
// The Gap slider and Bypass button live here so the parent can make this
// component fully-width; VoicePanel binds their APVTS attachments via the
// public `gapSlider` and `bypassButton` members.
class GatingDesigner : public juce::Component
{
public:
    GatingDesigner();

    void setSubdivision(int denominator);
    int  getSubdivision() const noexcept { return subdivisionDenom; }

    void setPattern(GatePattern* pattern);
    void setFilterPattern(GatePattern* pattern);
    void setPitchPattern(GatePattern* pattern);
    void setGap(float gap01);            // render-only mirror; also called by gapSlider.onValueChange
    void setLayer(GateLayer layer);
    GateLayer getLayer()           const noexcept { return currentLayer; }
    GateTool  selectedTool()       const noexcept { return currentTool; }
    int       selectedEnvIndex()   const noexcept { return selEnvIndex; }
    void setPlayhead(double beat01, bool visible);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

    static constexpr int kTotalBars = 2;

    // Public: VoicePanel binds APVTS attachments to these.
    juce::Slider     gapSlider;       // LinearHorizontal, 0..100 %
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

    // ── Toolbox ─────────────────────────────────────────────────────────────
    GateToolButton arrowBtn   { GateTool::Arrow };
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
    static constexpr int kHdr1H    = 24;   // tool / layer / bypass / grid dropdown
    static constexpr int kHdr2H    = 22;   // gap slider row
    static constexpr int kGridH    = 80;
    static constexpr int kPropsH   = 56;   // properties strip (prob knob + loop buttons)
    static constexpr int kHdrInset = 6;
    static constexpr int kDdW      = 88;
    static constexpr int kToolW    = 22;
    static constexpr int kToolGap  = 4;

    // ── Playhead ─────────────────────────────────────────────────────────────
    double playheadBeat01  = 0.0;
    bool   playheadVisible = false;

    // ── Envelope selection (Arrow tool) ──────────────────────────────────────
    int selEnvIndex = -1;

    // ── Properties strip ─────────────────────────────────────────────────────
    // Probability: compact rotary knob 1..100 integers.
    juce::Slider probKnob;           // Rotary, 1..100 step 1
    juce::Label  probLabel;

    // Loop mask: M dropdown + 8 small toggle buttons for loop positions.
    juce::Label    loopMLabel;
    DropdownSelect loopMDropdown;    // 1..8
    juce::TextButton loopBtn[8];     // position buttons 1..8

    bool propsUpdating = false;

    // ── Grid geometry ─────────────────────────────────────────────────────────
    int cellCount() const noexcept;
    juce::Rectangle<float> gridBounds() const noexcept;
    int cellAtX(float x) const noexcept;

    GatePattern* getActivePattern() const noexcept
    {
        if (currentLayer == GateLayer::Gater)  return boundPattern;
        if (currentLayer == GateLayer::Filter) return filterPattern;
        return pitchPattern;
    }
    GatePattern* getGhostPattern() const noexcept
    {
        // Show the gate layer as a timing reference ghost when editing filter/pitch.
        return (currentLayer == GateLayer::Gater) ? nullptr : boundPattern;
    }
    juce::Colour activeLayerColour() const noexcept;
    juce::Colour ghostLayerColour()  const noexcept;

    // ── Grab-handle (Pencil) ──────────────────────────────────────────────────
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
    int      dragOriginalEnd = -1;   // for StartEdge drag — keeps right edge fixed

    // ── Path cache (#801) ────────────────────────────────────────────────────
    std::vector<juce::Path> activePathCache;
    std::vector<juce::Path> ghostPathCache;
    bool pathsDirty = true;
    void markPathsDirty() noexcept { pathsDirty = true; }
    void rebuildPathCache();

    template <typename Fn> void withLock(GatePattern* pat, Fn&& fn);

    void clearSelection();
    void selectEnvelope(int idx);
    void updatePropsFromSelection();
    void writePropsToSelection();
    void updateLoopBtnEnabled();   // grey out buttons beyond loopM

    static constexpr float kHandleRadius = 6.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GatingDesigner)
};

} // namespace mu_tant
