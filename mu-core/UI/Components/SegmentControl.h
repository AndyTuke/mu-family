#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "MuLookAndFeel.h"

// 2–5 option toggle bar. Each segment is mutually exclusive.
// activeStyle controls the colour of the selected segment.
// drawStyle: Bar = single connected bar with dividers (default); Pills = individual rounded buttons;
// Lcd = each option a small LCD window set into the panel, the selected one backlit.
class SegmentControl : public juce::Component
{
public:
    enum class ActiveStyle { General, Positive, Warning };
    enum class DrawStyle  { Bar, Pills, Lcd };

    std::function<void(int index)> onChange;

    SegmentControl(std::initializer_list<juce::String> labels,
                   ActiveStyle style     = ActiveStyle::General,
                   DrawStyle   drawStyle = DrawStyle::Bar);

    void setSelectedIndex(int index, bool notify = false);
    // Replaces the labels (a count known only at run time, such as the page switch). The selection is kept when
    // it still exists, else the first segment.
    void setLabels(const std::vector<juce::String>& labels);
    int  getNumSegments() const noexcept { return (int) options.size(); }
    int  getSelectedIndex() const noexcept { return selectedIndex; }
    void setDrawStyle(DrawStyle d) { drawStyle = d; repaint(); }
    // Opt in to onChange firing when the already-selected segment is chosen again (e.g. a scene
    // row where a second click recalls the scene afresh). Default false: re-selecting is a no-op.
    void setFiresOnReselect(bool fires) noexcept { firesOnReselect = fires; }
    bool getFiresOnReselect() const noexcept { return firesOnReselect; }

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    std::vector<juce::String> options;
    int selectedIndex = 0;
    bool firesOnReselect = false;
    ActiveStyle style;
    DrawStyle   drawStyle;

    std::pair<juce::Colour, juce::Colour> activeColours() const noexcept;
};
