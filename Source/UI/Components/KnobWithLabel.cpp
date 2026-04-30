#include "KnobWithLabel.h"

KnobWithLabel::KnobWithLabel(const juce::String& label,
                             MuClidLookAndFeel::ColourIds categoryColour)
    : labelText(label), knobColour(categoryColour)
{
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setColour(juce::Slider::rotarySliderFillColourId,
                     MuClidLookAndFeel::colour(knobColour));
    slider.setColour(juce::Slider::rotarySliderOutlineColourId,
                     MuClidLookAndFeel::colour(MuClidLookAndFeel::segmentInactiveBorder));
    slider.setDoubleClickReturnValue(true, slider.getMinimum());

    slider.onValueChange = [this]
    {
        repaint();  // refresh value text in dead zone
        if (onStatusUpdate)
            onStatusUpdate(labelText, slider.getTextFromValue(slider.getValue()));
        if (onValueChanged)
            onValueChanged(slider.getValue());
    };

    slider.addMouseListener(this, false);
    addAndMakeVisible(slider);
}

void KnobWithLabel::setRange(double min, double max, double step)
{
    slider.setRange(min, max, step);
}

void KnobWithLabel::setValue(double v, juce::NotificationType n)
{
    slider.setValue(v, n);
}

double KnobWithLabel::getValue() const
{
    return slider.getValue();
}

void KnobWithLabel::resized()
{
    const int labelH = 14;
    slider.setBounds(0, 0, getWidth(), getHeight() - labelH);
}

void KnobWithLabel::mouseEnter(const juce::MouseEvent&)
{
    if (onStatusUpdate)
        onStatusUpdate(labelText, slider.getTextFromValue(slider.getValue()));
}

void KnobWithLabel::paint(juce::Graphics& g)
{
    const int labelH = 14;

    // Label below knob
    g.setFont(juce::Font(10.0f));
    g.setColour(MuClidLookAndFeel::colour(MuClidLookAndFeel::labelText));
    g.drawText(labelText,
               juce::Rectangle<int>(0, getHeight() - labelH, getWidth(), labelH),
               juce::Justification::centred, true);

    // Value text in the dead zone (5–7 o'clock gap at the bottom of the arc)
    const float sliderH = (float)(getHeight() - labelH);
    const float radius  = juce::jmin((float)getWidth(), sliderH) * 0.5f - 2.0f;
    const float cy      = sliderH * 0.5f;
    const int   valueY  = (int)(cy + radius * 0.75f) - 5;

    g.setFont(juce::Font(8.0f));
    g.setColour(MuClidLookAndFeel::colour(MuClidLookAndFeel::valueText));
    g.drawText(slider.getTextFromValue(slider.getValue()),
               0, valueY, getWidth(), 11,
               juce::Justification::centred, true);
}
