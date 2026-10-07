#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Plugin/ProcessorBase.h"
#include "Sequencer/StepPattern.h"
#include "UI/Components/MuLookAndFeel.h"
#include "UI/Components/KnobWithLabel.h"

// GrooveGrid — the 909 step editor for the SELECTED lane: a single row of 16 cells
// (the lane chosen in the sidebar), mirroring mu-tant's per-voice editor shape.
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

    // Preferred total height for the single-lane editor (knob strip + title + step row).
    static constexpr int kStepEditorHeight = 128;

    // Metal style: the host draws two raised boxes behind this editor — Groove (Swing /
    // Accent) on the left, kGrooveBoxW wide, then a box gap and the lane's steps — kBoxH tall.
    static constexpr int kHeaderH    = MuLookAndFeel::kKnobSize2H;   // Swing/Accent knob strip
    static constexpr int kGrooveBoxW = 2 * MuLookAndFeel::kKnobSize2W + MuLookAndFeel::kSpaceS
                                     + 2 * MuLookAndFeel::kSubPanelScrewClear;
    static constexpr int kBoxH       = kHeaderH + 2 * MuLookAndFeel::kSpaceS;

    // Highlight the row matching the sidebar selection.
    void setSelectedTrack(int t) { selectedTrack = t; repaint(); }

    void paint(juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override { resized(); repaint(); }   // metal style moves Swing / Accent
    void mouseDown(const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    bool cellAt(juce::Point<int> p, int& track, int& step) const;
    juce::Rectangle<int> gridArea() const;
    void paintMetal(juce::Graphics&, int track, int steps, juce::Colour col);
    juce::Rectangle<int> rowArea() const;
    juce::Colour trackColour(int t) const;

    ProcessorBase& proc;
    StepPattern&   pattern;

    KnobWithLabel swingKnob { "Swing" }, accentKnob { "Accent" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> swingAtt, accentAtt;

    int selectedTrack = 0;
    int playheadStep  = -1;

    static constexpr int kTitleH  = 20;   // lane-name band above the step row (flat style)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GrooveGrid)
};

} // namespace mu_on
