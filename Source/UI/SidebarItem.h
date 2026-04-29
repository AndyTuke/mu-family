#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "RhythmCircle.h"
#include "Components/MuClidLookAndFeel.h"
#include "../Sequencer/Rhythm.h"

// One entry in the RhythmSidebar. Shows a small RhythmCircle, colour dot, and rhythm name.
// Right-edge tab line when selected. onSelected fires on click.
class SidebarItem : public juce::Component
{
public:
    explicit SidebarItem(int index);

    void setRhythm(const Rhythm* r, juce::Colour colour);
    void setSelected(bool s);
    void pulse();

    std::function<void(int)> onSelected;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void resized() override;

private:
    int           rhythmIndex;
    const Rhythm* rhythm = nullptr;
    juce::Colour  rhythmColour { juce::Colours::transparentBlack };
    bool          selected = false;

    RhythmCircle miniCircle;
};
