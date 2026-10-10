#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "MuLookAndFeel.h"

// Numeric display with ▲/▼ arrows and step-size buttons (×1, ×5, ×10).
// Supports direct text entry on double-click. Whole numbers by default; setDecimals(n) makes it
// a fixed-decimals value (the BPM field shows "127.5"), with the double API below.
class NudgeInput : public juce::Component
{
public:
    std::function<void(int value)> onChange;        // fired when decimals == 0
    std::function<void(double value)> onChangeD;    // fired when decimals > 0

    NudgeInput(const juce::String& label, int minValue, int maxValue, int defaultValue = 0);

    void setValue(int v, bool notify = false);
    int  getValue() const noexcept { return (int) std::lround(value); }

    // Decimal mode: the value is rounded to `n` places (0 = whole numbers, the default),
    // always shown with that many ("120.0"), and parsed from text entry with ',' or '.'.
    void   setDecimals(int n);
    void   setValueD(double v, bool notify = false);
    double getValueD() const noexcept { return value; }
    // A plain arrow click steps by `step` (when the step buttons are hidden); Shift+click by `fine`.
    void   setStep(double step)     { baseStep = step; }
    // Show a leading + on positive values (an offset: +25).
    void   setShowSign(bool show)   { showSign = show; repaint(); }
    juce::String valueText() const;   // what the display shows (also the text editor's start text)
    void   setFineStep(double fine) { fineStep = fine; }
    // When false: hides step-size (1/5/10) buttons; if the label string is non-empty,
    // it is drawn in the space below the value display instead.
    void setShowStepButtons(bool show) { showStepBtns = show; resized(); repaint(); }
    // When true (and showStepBtns is false): draws the label inside the display area to
    // the left of the value on the same row, rather than below. Useful for compact inline
    // labelling (e.g. "BPM  120").
    void setLabelInline(bool inl) { labelInline = inl; resized(); repaint(); }

    void resized() override;
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

private:
    juce::String label;
    double minVal, maxVal, value;
    int    decimals  = 0;
    bool   showSign  = false;
    double baseStep  = 1.0;    // arrow step when the step buttons are hidden
    double fineStep  = 1.0;    // Shift+arrow step
    int stepSize   = 1;
    bool showStepBtns  = true;
    bool labelInline   = false;

    enum class HitZone { None, Up, Down, Step1, Step5, Step10, Display };
    HitZone getZone(juce::Point<int> p) const;
    void nudge(int direction, bool fine);
    void showEditor();

    juce::Rectangle<int> upArrowBounds, downArrowBounds;
    juce::Rectangle<int> step1Bounds, step5Bounds, step10Bounds;
    juce::Rectangle<int> displayBounds, labelBounds;
};
