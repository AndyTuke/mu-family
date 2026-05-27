#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "MuLookAndFeel.h"

// Single-line parameter feedback bar. Shows last-touched param name + value.
// Rhythm colour tag shown when the param belongs to a rhythm.
class StatusBar : public juce::Component
{
public:
    StatusBar();

    // Call when any knob changes. rhythmColour is transparent if not a rhythm param.
    void showParam(const juce::String& paramName,
                   const juce::String& value,
                   juce::Colour rhythmColour = juce::Colours::transparentBlack);

    void paint(juce::Graphics& g) override;

private:
    juce::String currentName;
    juce::String currentValue;
    juce::Colour tagColour { juce::Colours::transparentBlack };

    static constexpr int kTagWidth = 4;
};
