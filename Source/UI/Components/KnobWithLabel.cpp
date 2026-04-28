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
        if (onStatusUpdate)
            onStatusUpdate(labelText, slider.getTextFromValue(slider.getValue()));
    };

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

void KnobWithLabel::paint(juce::Graphics& g)
{
    const int labelH = 14;
    g.setFont(juce::Font(10.0f));
    g.setColour(MuClidLookAndFeel::colour(MuClidLookAndFeel::labelText));
    g.drawText(labelText,
               juce::Rectangle<int>(0, getHeight() - labelH, getWidth(), labelH),
               juce::Justification::centred, true);
}
