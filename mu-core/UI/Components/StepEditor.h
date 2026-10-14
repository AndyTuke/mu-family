#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "MuLookAndFeel.h"

// Bipolar bar-graph step editor. Centre = zero, +100 = top, -100 = bottom.
// All bars drawn in the modulator colour regardless of sign.
// Step count is derived from the parent and passed in; bars truncate on resize.
class StepEditor : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void(int stepIndex, float value)> onStepChanged;
    // Level-cell mode only: the cell the mouse is on (hover, click or drag) and its level, 0 = lowest.
    std::function<void(int cell, int level)> onCellStatus;

    StepEditor();

    void setSteps(const std::vector<float>& values);
    const std::vector<float>& getSteps() const noexcept { return steps; }
    void setStepCount(int count);
    // One step as a fraction of the loop (step/loop). When set in (0,1) the cells tile the
    // editor at k·stepFraction with a narrower partial final cell (matching the audio +
    // the smooth editor); 0 or ≥1 falls back to equal 1/count cells.
    void setStepFraction(float frac) { stepFraction = frac; repaint(); }
    void setBarColour(juce::Colour c);
    // Unipolar: bars from bottom (0 baseline), all values 0..+100.
    // Bipolar: bars from centre (0 baseline), values -100..+100 (default).
    void setUnipolar(bool u) { unipolar = u; repaint(); }

    // Quantize dragged values to N evenly-spaced levels spanning the active range.
    // levels=0 (default) → continuous. levels=7 → 7 snap points (useful for pitch.octave).
    void setQuantization(int levels) { quantizeLevels = levels; }

    // Level cells (n = 2 or 3; 0 = off, the default): every cell is one whole lens whose strength is its
    // level (Off, On, Accented), not a bar. Values stay 0 / 50 / 100 (n=3) or 0 / 100 (n=2), so setSteps
    // and onStepChanged are unchanged. Click cycles the level up (Shift: down); a drag paints the state
    // the first click set into each cell entered; the mouse height is ignored.
    void setLevelCells(int n);
    int  getLevelCells() const noexcept { return levelCells; }
    int  levelOfValue(float v) const noexcept;   // value (0..100) -> level 0..levelCells-1
    float valueOfLevel(int level) const noexcept;

    // playhead: 0.0 = start, 1.0 = end of loop
    void setPlayheadPhase(float phase);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    void paintLamps(juce::Graphics& g);   // metal style: the rings' lamp display
    std::vector<float> steps;
    juce::Colour barColour { MuLookAndFeel::colour(MuLookAndFeel::stepEditorBar) };
    float playheadPhase = 0.0f;
    bool  unipolar      = false;
    int   quantizeLevels = 0;
    float stepFraction  = 0.0f;   // step/loop; 0 or ≥1 → equal 1/count cells
    int   levelCells    = 0;      // 0 = bar mode; 2 or 3 = level-cell mode
    int   paintLevel    = 0;      // level-cell drag: the state being painted
    int   lastPaintCell = -1;     // level-cell drag: the last cell written, so a cell is set once per entry

    float cellEdge(int i) const;
    int   stepAtPhase(float phase) const;
    int hitStepIndex(int x) const;
    float yToValue(int y) const;
    void applyAt(int x, int y);
    void setCellLevel(int idx, int level);
    void reportCell(int x);
};
