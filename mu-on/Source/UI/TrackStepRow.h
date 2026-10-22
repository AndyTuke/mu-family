#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/TrackHost.h"   // what the row reads from the layer's host
#include "Sequencer/StepRow.h"

#include <functional>

// TrackStepRow - one track's 16 steps as lamps behind dark lenses: the single drawing of a 909 step row.
// StepRows paints each of its rows with paintCells; this component is the same row on its own (a mu-Tronic
// Track layer shows its one row in the Steps box). Left-click toggles a step on/off, the small button in its top-right corner
// its accent, and right-click opens the step's state menu (a Hat step can be Closed or Open). No name gutter, no selected-row outline, no row-click callback:
// the layer is named by the header and the sidebar. The cells are in the layer's palette colour, and a Rumble
// (no steps) shows the same lenses unlit and dimmed. The component draws the cells over its whole area;
// whoever places it gives it the row height StepRows gives its rows. Metal style only.
namespace mu_on
{

class TrackStepRow : public juce::Component, private juce::Timer
{
public:
    explicit TrackStepRow(TrackHost& host);
    ~TrackStepRow() override { stopTimer(); }

    // Show layer `slot`'s row.
    void setLayer(int slot);

    // A hover over a step: the control's name and its value, for the shell's status bar.
    std::function<void(const juce::String& name, const juce::String& value)> onStatusUpdate;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void lookAndFeelChanged() override { repaint(); }

    // ── The shared row look (StepRows calls these per row) ───────────────────
    // Paint the 16 cells of one row into `cells`. `showSteps` false draws the lenses unlit (a Rumble, which has no
    // steps). `playheadStep` is the step the transport is on (-1: none). `beatNumbers` engraves 1/5/9/13 on the
    // beat-group starts (StepRows does it on its first row only; a lone row always does).
    static void paintCells(juce::Graphics&, juce::Rectangle<int> cells, const StepRow& row, bool showSteps,
                           juce::Colour colour, int playheadStep, bool beatNumbers);
    // The step under `p` in a row whose cells are `cells` (the caller has checked the point is inside).
    static int  stepAt(juce::Rectangle<int> cells, juce::Point<int> p);
    // A click on one step of a row whose cells are `cells`, at point `p` (the caller has checked the point is inside).
    // Left on the step's small corner button toggles its accent (an off step turns on accented); left elsewhere toggles
    // the step on / off. Right selects the step: a menu of its states (a Hat: Off, Closed, Open; the others: Off, On).
    // `changed` runs after any edit, so the caller repaints (a menu answers later).
    static void clickStep(StepRow& row, int step, juce::Rectangle<int> cells, juce::Point<int> p, bool rightClick,
                          bool hat, std::function<void()> changed);
    // The cell of `step` and the accent button inside its top-right corner.
    static juce::Rectangle<float> cellRect(juce::Rectangle<int> cells, int step);
    static juce::Rectangle<float> accentButtonRect(juce::Rectangle<float> cell);

private:
    void timerCallback() override;

    TrackHost& host;
    int slot         = 0;
    int playheadStep = -1;
    juce::uint64 shownSignature = 0;   // the colour and engine the row was last drawn with
    int hoverStep    = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackStepRow)
};

} // namespace mu_on
