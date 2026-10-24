#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/TrackHost.h"   // what the row reads from the layer's host
#include "Sequencer/StepRow.h"

#include <functional>

// TrackStepRow - one track's 16 steps as lamps behind dark lenses: the single drawing of a 909 step row.
// StepRows paints each of its rows with paintCells; this component is the same row on its own (a mu-Tronic
// Track layer shows its one row in the Steps box). Clicking an empty step creates it (it comes back as it was when deleted)
// and selects it, clicking a step selects it (the Step panel then edits its probability, loop ratio, a Hat's sound, a
// Kick's or Bass's octave and note), the small button in its top-left corner toggles its accent and the cross in its top-right
// corner deletes it. No name gutter, no selected-row outline, no row-click callback:
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

    // The step outlined as selected (-1: none), and the callback for a right-click that selects another.
    void setSelectedStep(int step) { selectedStep = step; repaint(); }
    std::function<void(int step)> onStepSelected;

    // A hover over a step: the control's name and its value, for the shell's status bar.
    std::function<void(const juce::String& name, const juce::String& value)> onStatusUpdate;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void lookAndFeelChanged() override { repaint(); }

    // ── The shared row look (StepRows calls these per row) ───────────────────
    // Paint the 16 cells of one row into `cells`. `showSteps` false draws the lenses unlit (a Rumble, which has no
    // steps). `playheadStep` is the step the transport is on (-1: none). `beatNumbers` engraves 1/5/9/13 on the
    // beat-group starts (StepRows does it on its first row only; a lone row always does).
    // What a row shows beyond the lamps: a Bass's ties and note names, a Hat's open / closed icon, and the steps this pass will skip.
    struct CellOptions
    {
        bool ties = false;                              // draw the bar between tied steps (a Bass)
        bool hatIcons = false;                          // draw the open / closed icon (a Hat)
        std::function<juce::String(int)> noteName;      // the note a step plays, shown in its cell (a Bass); empty: none
        std::uint16_t dimMask = 0;                      // bit s: step s is on but is skipped in this pass (loop ratio or probability)
    };
    static CellOptions optionsFor(TrackHost& host, int slot);
    // The key a Kick's or Bass's tune sits in: its MIDI note after the master key and scale snap, and that root and scale.
    struct TuneKey { int baseMidi = 36; int root = 0; int scale = 11; };
    static TuneKey tuneKey(TrackHost& host, int slot, EngineType engine);
    static void paintCells(juce::Graphics&, juce::Rectangle<int> cells, const StepRow& row, bool showSteps,
                           juce::Colour colour, int playheadStep, bool beatNumbers, int selectedStep = -1,
                           const CellOptions& options = {});
    // The step under `p` in a row whose cells are `cells` (the caller has checked the point is inside).
    static int  stepAt(juce::Rectangle<int> cells, juce::Point<int> p);
    // A click on one step of a row whose cells are `cells`, at point `p` (the caller has checked the point is inside).
    // On a step that is on, the top-left button toggles its accent and the top-right cross deletes it (returns false: no
    // selection). Anywhere else the click selects the step (returns true; the caller shows its Step panel), and on an empty
    // step it first creates it.
    static bool clickStep(StepRow& row, int step, juce::Rectangle<int> cells, juce::Point<int> p, bool rightClick);
    // Dragging from a step onto the one next to it ties them (a Bass slides from the first into the second): both turn on
    // and the later one is marked tied; doing it again over a pair that was already tied (both on before the drag) unties
    // them. `startedOn` is whether the step the drag began on was on before the click.
    static void dragTie(StepRow& row, int from, int to, bool startedOn);

    // Dragging up or down on a step edits it (a drag that goes sideways more than up or down stays a tie drag):
    //   plain  - a Bass: its note moves through the master key's scale, kPixelsPerDegree of travel per degree;
    //            a Hat: up makes it open, down closed;
    //   Shift  - a Kick, Bass or Chord: its octave, kPixelsPerOctave of travel per octave;
    //   Alt    - any engine: up accents the step, down clears the accent.
    // The mode is fixed when the drag starts; values are taken from the step as it was at the press.
    enum class DragMode { None, Note, Octave, Accent, Voice };
    struct StepDrag { bool active = false; DragMode mode = DragMode::None; int startSemis = 0; int startOct = 0; };
    static constexpr int kPitchDragStartPx = 5;
    static constexpr int kPixelsPerDegree  = 8;
    static constexpr int kPixelsPerOctave  = 20;
    // Apply the drag `e` (from the press on `step`) to the row; true while it is an edit drag (the caller then skips the
    // tie), with the status bar's name and value for what changed.
    static bool dragStepValue(TrackHost& host, int slot, int step, StepDrag& drag, const juce::MouseEvent& e,
                              juce::String& statusName, juce::String& statusValue);
    // The cell of `step`, the accent button inside its top-left corner and the delete cross inside its top-right.
    static juce::Rectangle<float> cellRect(juce::Rectangle<int> cells, int step);
    static juce::Rectangle<float> accentButtonRect(juce::Rectangle<float> cell);
    static juce::Rectangle<float> deleteButtonRect(juce::Rectangle<float> cell);

private:
    void timerCallback() override;

    TrackHost& host;
    int slot         = 0;
    int playheadStep = -1;
    long long shownPass = -2;          // the pass the dimmed steps were drawn for
    juce::uint64 shownSignature = 0;   // the colour and engine the row was last drawn with
    int hoverStep    = -1;
    int selectedStep = -1;
    int dragStep     = -1;     // the step a left-drag has reached (-1: no drag)
    bool dragStartedOn = false;
    StepDrag stepDrag;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackStepRow)
};

} // namespace mu_on
