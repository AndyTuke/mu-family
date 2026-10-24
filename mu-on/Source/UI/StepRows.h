#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/TrackHost.h"   // what the rows read from the layer's host
#include "UI/TrackStepRow.h"   // the shared row look and the step drag helpers
#include "UI/Components/MuLookAndFeel.h"

#include <array>
#include <functional>

// StepRows — the 909 step editor: one row of 16 steps per live layer, in sidebar order. Every row has
// the same height (the component's height / kMaxLayers), top-aligned, so adding a layer never resizes
// the others. Each row opens with the layer's number and engine name in its colour; the selected layer's
// row is outlined in that colour. Left-click toggles a step on/off, the corner button its accent, right-click selects it, and a click
// anywhere in a row selects that layer. A Rumble layer has no steps: its row is dimmed, not editable,
// and labelled with the engine name. A moving playhead column tracks the transport. Metal style only.
namespace mu_on
{

class StepRows : public juce::Component, private juce::Timer
{
public:
    explicit StepRows(TrackHost& host);
    ~StepRows() override { stopTimer(); }

    // Outline the row of layer `idx` (the layer shown by the rest of the panel).
    void setSelected(int idx);
    // The step outlined in the selected row (-1: none), and the callback for a right-click that selects a step: (row, step).
    void setSelectedStep(int step) { selectedStep = step; repaint(); }
    std::function<void(int row, int step)> onStepSelected;

    std::function<void(int)> onRowClicked;   // a click landed in layer idx's row: the editor selects it

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void lookAndFeelChanged() override { repaint(); }

    // Unscaled width of the name gutter at the start of each row.
    static constexpr int kLabelW = 62;

private:
    void timerCallback() override;
    int  rowHeight() const noexcept;
    juce::Rectangle<int> rowArea(int row) const;      // the whole row
    juce::Rectangle<int> cellsArea(int row) const;    // the 16 cells of the row
    bool cellAt(juce::Point<int> p, int& row, int& step) const;
    juce::Colour layerColour(int row) const;
    void paintRow(juce::Graphics&, int row);
    juce::uint64 signature() const;                   // changes when a row's colour, engine or the count does

    TrackHost& host;
    int selected      = 0;
    int selectedStep  = -1;
    int dragRow = -1, dragStep = -1;   // a left-drag across steps (to tie them): the row and the step it has reached
    bool dragStartedOn = false;
    TrackStepRow::StepDrag stepDrag;   // a vertical drag on a step edits it (note, octave, accent, Hat open / closed)
    int playheadStep  = -1;
    long long shownPass = -2;          // the pass the dimmed steps were drawn for
    juce::uint64 shownSignature = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StepRows)
};

} // namespace mu_on
