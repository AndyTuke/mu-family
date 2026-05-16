#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// #372: shared depth + curve slider pair used by ModulatorEditor::AssignmentRow and
// ModMatrixPanel::MatrixRow. Replaces the duplicated raw juce::Slider setup that
// previously violated CLAUDE.md's "All UI uses the shared component library — never
// build a one-off version of a standard control" rule. Both slider styles are
// bipolar -100..+100 with a centre detent (double-click on the curve returns to 0).
class BipolarSliderRow : public juce::Component
{
public:
    BipolarSliderRow();

    // Value accessors mirror the underlying juce::Slider API for direct
    // setValue(...dontSendNotification...) use in parent loadFrom* paths.
    void  setDepth(double v, juce::NotificationType n = juce::sendNotificationSync);
    void  setCurve(double v, juce::NotificationType n = juce::sendNotificationSync);
    float getDepth() const noexcept { return (float) depthSlider.getValue(); }
    float getCurve() const noexcept { return (float) curveSlider.getValue(); }

    // Fired on user interaction only (programmatic setValue calls should use
    // dontSendNotification to avoid re-entry into the model).
    std::function<void(float)> onDepthChange;
    std::function<void(float)> onCurveChange;

    // Column widths used by parents to lay out depth + curve + neighbouring controls.
    static constexpr int kDepthWidth = 130;
    static constexpr int kCurveWidth = 70;

    void resized() override;

private:
    juce::Slider depthSlider;
    juce::Slider curveSlider;
};
