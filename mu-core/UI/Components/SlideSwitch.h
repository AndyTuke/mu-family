#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "MuLookAndFeel.h"

// Vertical two-position slide switch: a small disc that slides between the ends of a
// recessed track, with a label beside each end. Index 0 = top, 1 = bottom. The API
// mirrors SegmentControl so a two-option segment can be swapped for one directly.
//
// Click anywhere to flip it; dragging the disc past the midpoint commits. Drawing is
// MuLookAndFeel::drawSlideSwitch, in the same language as the family knob.
class SlideSwitch : public juce::Component, private juce::Timer
{
public:
    std::function<void(int index)> onChange;

    // accentId is normally the paired knob's colour, so the two read as one unit.
    SlideSwitch(juce::String topLabel, juce::String bottomLabel,
                MuLookAndFeel::ColourIds accentId);

    void setSelectedIndex(int index, bool notify = false);
    int  getSelectedIndex() const noexcept { return selectedIndex; }

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    void timerCallback() override;
    void select(int index);   // user gesture: animates and notifies

    juce::String topLabel, bottomLabel;
    MuLookAndFeel::ColourIds accentId;

    int   selectedIndex = 0;
    float position      = 0.0f;   // drawn disc position, 0 = top, 1 = bottom
    float dragStartY    = 0.0f;
    bool  dragged       = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SlideSwitch)
};
