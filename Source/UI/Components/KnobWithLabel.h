#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "MuClidLookAndFeel.h"

// Rotary slider + category colour + label below + optional status bar callback.
class KnobWithLabel : public juce::Component
{
public:
    std::function<void(const juce::String& name, const juce::String& value)> onStatusUpdate;

    KnobWithLabel(const juce::String& label,
                  MuClidLookAndFeel::ColourIds categoryColour = MuClidLookAndFeel::knobEuclidean);

    juce::Slider& getSlider() noexcept { return slider; }

    void setRange(double min, double max, double step = 0.0);
    void setValue(double v, juce::NotificationType n = juce::dontSendNotification);
    double getValue() const;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    juce::Slider slider;
    juce::String labelText;
    MuClidLookAndFeel::ColourIds knobColour;
};
