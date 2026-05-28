#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/Components/DropdownSelect.h"
#include "UI/Components/MuLookAndFeel.h"
#include "Sequencer/GatePattern.h"

namespace mu_tant
{

// Per-voice gating designer (placeholder for the eventual interactive gate
// editor). Represents 2 bars of gate pattern as a full-width rectangle; a
// note-length dropdown at the top sets the subdivision-grid spacing
// (1/4, 1/8, 1/16, ...). No gate data is wired yet — this is the visual
// scaffold that the real drawable gate editor will sit inside once the
// sequencer model lands.
// Edit tool the toolbox selects. Roles are wired when the drawable editor
// lands; for now selecting a tool just highlights its button.
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

class GatingDesigner : public juce::Component
{
public:
    GatingDesigner();

    // Set the subdivision note value. Affects the gridline density.
    // 4 = 1/4 → 8 cells, 8 = 1/8 → 16 cells, 16 = 1/16 → 32, 32 = 1/32 → 64.
    void setSubdivision(int denominator);
    int  getSubdivision() const noexcept { return subdivisionDenom; }

    // Bind to a per-voice GatePattern — clicking the subdivision dropdown
    // writes back to `pattern->subdivision`. Pass nullptr to unbind. Pattern
    // ownership stays with the caller (PluginProcessor).
    void setPattern(GatePattern* pattern);

    GateTool selectedTool() const noexcept { return currentTool; }

    // Move the playback timeline. `beat01` is the song position normalised to
    // 0..1 across the 2-bar grid; `visible` hides the line when stopped.
    void setPlayhead(double beat01, bool visible);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

    // Total bars represented by the strip. Fixed at 2 per the design spec.
    static constexpr int kTotalBars = 2;

private:
    juce::Label    subdivLabel;
    DropdownSelect subdivDropdown;

    // Toolbox: pencil / eraser / glue / reverse (roles TBD).
    GateToolButton pencilBtn  { GateTool::Pencil };
    GateToolButton eraserBtn  { GateTool::Eraser };
    GateToolButton glueBtn    { GateTool::Glue };
    GateToolButton reverseBtn { GateTool::Reverse };
    GateTool       currentTool = GateTool::Pencil;
    void selectTool(GateTool t);

    int subdivisionDenom = 16;   // 1/16 default — 32 cells over 2 bars

    // Bound GatePattern — receives subdivision writes. nullptr when no voice
    // is selected (e.g. just-constructed VoicePanel before setVoice runs).
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GatingDesigner)
};

} // namespace mu_tant
