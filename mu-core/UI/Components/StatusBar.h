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

    // A small "Support" text link at the right edge (unlicensed copies only). Clicking it calls
    // onSupportClicked; hidden by default.
    void setSupportLinkVisible(bool visible);
    std::function<void()> onSupportClicked;

    void paint(juce::Graphics& g) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    juce::Rectangle<int> supportLinkBounds() const;

    bool supportLinkVisible = false;
    bool supportLinkHover   = false;

    juce::String currentName;
    juce::String currentValue;
    juce::Colour tagColour { juce::Colours::transparentBlack };

    static constexpr int kTagWidth = 4;
};
