#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "MuClidLookAndFeel.h"

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
    void setBarColour(juce::Colour c);
    // Unipolar: bars from bottom (0 baseline), all values 0..+100.
    // Bipolar: bars from centre (0 baseline), values -100..+100 (default).
    void setUnipolar(bool u) { unipolar = u; repaint(); }

    // playhead: 0.0 = start, 1.0 = end of loop
    void setPlayheadPhase(float phase);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;

private:
    std::vector<float> steps;
    juce::Colour barColour { MuClidLookAndFeel::colour(MuClidLookAndFeel::stepEditorBar) };
    float playheadPhase = 0.0f;
    bool  unipolar      = false;

    int hitStepIndex(int x) const;
    float yToValue(int y) const;
    void applyAt(int x, int y);
};
