#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Plugin/ProcessorBase.h"
#include "Sequencer/StepPattern.h"

// GrooveGrid — the 909 step grid: 4 instrument rows (Kick/Bass/Hat/Snare) × 16 cells.
// Left-click toggles a step on/off; right-click toggles its accent. A moving playhead
// column tracks the transport. Global Swing + Accent are rotary sliders bound to the
// product APVTS params. Reads track names/colours from ProcessorBase.
namespace mu_on
{

class GrooveGrid : public juce::Component, private juce::Timer
{
public:
    GrooveGrid(ProcessorBase& processor, StepPattern& patternToEdit);
    ~GrooveGrid() override { stopTimer(); }

    // Highlight the row matching the sidebar selection.
    void setSelectedTrack(int t) { selectedTrack = t; repaint(); }

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    bool cellAt(juce::Point<int> p, int& track, int& step) const;
    juce::Rectangle<int> gridArea() const;
    juce::Colour trackColour(int t) const;

    ProcessorBase& proc;
    StepPattern&   pattern;

    juce::Slider swingSlider, accentSlider;
    juce::Label  swingLabel,  accentLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> swingAtt, accentAtt;

    int selectedTrack = 0;
    int playheadStep  = -1;

    static constexpr int kHeaderH = 46;
    static constexpr int kLabelW  = 64;
    static constexpr int kRowGap  = 4;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GrooveGrid)
};

} // namespace mu_on
