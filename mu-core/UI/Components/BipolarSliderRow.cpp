#include "BipolarSliderRow.h"

BipolarSliderRow::BipolarSliderRow()
{
    using mu_ui::s;
    // Depth: horizontal bar with text box on the right. Bipolar -100..+100.
    depthSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    depthSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, s(44), s(18));
    depthSlider.setRange(-100.0, 100.0, 0.1);
    depthSlider.setValue(0.0, juce::dontSendNotification);

    // curve knob: rotary, bipolar with detent at 0.
    curveSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    curveSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, s(34), s(18));
    curveSlider.setRange(-100.0, 100.0, 0.1);
    curveSlider.setValue(0.0, juce::dontSendNotification);
    curveSlider.setDoubleClickReturnValue(true, 0.0);
    curveSlider.setTooltip("Curve: log .. linear .. exp");

    depthSlider.onValueChange = [this]
    {
        if (onDepthChange) onDepthChange((float) depthSlider.getValue());
    };
    curveSlider.onValueChange = [this]
    {
        if (onCurveChange) onCurveChange((float) curveSlider.getValue());
    };

    addAndMakeVisible(depthSlider);
    addAndMakeVisible(curveSlider);
}

void BipolarSliderRow::setDepth(double v, juce::NotificationType n)
{
    depthSlider.setValue(v, n);
}

void BipolarSliderRow::setCurve(double v, juce::NotificationType n)
{
    curveSlider.setValue(v, n);
}

void BipolarSliderRow::resized()
{
    using mu_ui::s;
    const int h = getHeight();
    const int dW = s(kDepthWidth);
    const int cW = s(kCurveWidth);
    depthSlider.setBounds(0,        0, dW, h);
    curveSlider.setBounds(dW + s(2), 0, cW, h);
}
