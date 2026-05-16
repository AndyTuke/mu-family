#include "BipolarSliderRow.h"

BipolarSliderRow::BipolarSliderRow()
{
    // Depth: horizontal bar with text box on the right. Bipolar -100..+100.
    depthSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    depthSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 18);
    depthSlider.setRange(-100.0, 100.0, 0.1);
    depthSlider.setValue(0.0, juce::dontSendNotification);

    // #224 curve knob: rotary, bipolar with detent at 0.
    curveSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    curveSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 34, 18);
    curveSlider.setRange(-100.0, 100.0, 0.1);
    curveSlider.setValue(0.0, juce::dontSendNotification);
    curveSlider.setDoubleClickReturnValue(true, 0.0);
    curveSlider.setTooltip("Curve: log .. linear .. exp (#224)");

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
    const int h = getHeight();
    depthSlider.setBounds(0,                       0, kDepthWidth, h);
    curveSlider.setBounds(kDepthWidth + 2,         0, kCurveWidth, h);
}
