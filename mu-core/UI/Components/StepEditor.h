#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "MuLookAndFeel.h"

// Bipolar bar-graph step editor. Centre = zero, +100 = top, -100 = bottom.
// All bars drawn in the modulator colour regardless of sign.
// Step count is derived from the parent and passed in; bars truncate on resize.
class StepEditor : public juce::Component
{
public:
    std::function<void(int stepIndex, float value)> onStepChanged;

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

    // playhead: 0.0 = start, 1.0 = end of loop
    void setPlayheadPhase(float phase);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;

private:
    std::vector<float> steps;
    juce::Colour barColour { MuLookAndFeel::colour(MuLookAndFeel::stepEditorBar) };
    float playheadPhase = 0.0f;
    bool  unipolar      = false;
    int   quantizeLevels = 0;
    float stepFraction  = 0.0f;   // step/loop; 0 or ≥1 → equal 1/count cells

    int hitStepIndex(int x) const;
    float yToValue(int y) const;
    void applyAt(int x, int y);
};
