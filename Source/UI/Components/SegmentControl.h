#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "MuClidLookAndFeel.h"

// 2–5 option toggle bar. Each segment is mutually exclusive.
// activeStyle controls the colour of the selected segment.
class SegmentControl : public juce::Component
{
public:
    enum class ActiveStyle { General, Positive, Warning };

    std::function<void(int index)> onChange;

    SegmentControl(std::initializer_list<juce::String> labels,
                   ActiveStyle style = ActiveStyle::General);

    void setSelectedIndex(int index, bool notify = false);
    int  getSelectedIndex() const noexcept { return selectedIndex; }

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    std::vector<juce::String> options;
    int selectedIndex = 0;
    ActiveStyle style;

    std::pair<juce::Colour, juce::Colour> activeColours() const noexcept;
};
