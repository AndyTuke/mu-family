#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "MuClidLookAndFeel.h"

// Integer display with ▲/▼ arrows and step-size buttons (×1, ×5, ×10).
// Supports direct text entry on double-click.
class NudgeInput : public juce::Component
{
public:
    std::function<void(int value)> onChange;

    NudgeInput(const juce::String& label, int minValue, int maxValue, int defaultValue = 0);

    void setValue(int v, bool notify = false);
    int  getValue() const noexcept { return currentValue; }

    void resized() override;
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

private:
    juce::String label;
    int minVal, maxVal, currentValue;
    int stepSize = 1;

    enum class HitZone { None, Up, Down, Step1, Step5, Step10, Display };
    HitZone getZone(juce::Point<int> p) const;
    void nudge(int delta);
    void showEditor();

    juce::Rectangle<int> upArrowBounds, downArrowBounds;
    juce::Rectangle<int> step1Bounds, step5Bounds, step10Bounds;
    juce::Rectangle<int> displayBounds;
};
