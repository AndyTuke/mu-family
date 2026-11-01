#pragma once

#include "Sequencer/Timeline.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

// The Phrase layer's timeline view (docs/mu-tilate/design-ui.md, "Timeline"): a ruler in bars and beats over one row of slice
// blocks. It shows the Timeline it is given and reports what the user asked for; it edits nothing itself. A click on empty row
// asks to place the slice the host names, a drag moves a block (Alt = free, Ctrl = copy), Delete removes the selected block.
// A long timeline is zoomed and scrolled: the wheel scrolls, Ctrl+wheel zooms around the pointer, and while it plays the view
// pages on to keep the playhead in sight. Dragging a block's right edge sets its length (Alt = free, else the grid).
namespace mu_tilate
{

class PhraseTimeline : public juce::Component
{
public:
    PhraseTimeline() { setWantsKeyboardFocus(true); }

    // The timeline to show, how long each slice is in beats (index = slice), the snap grid in beats (0 = none) and the selected block.
    void setTimeline(const Timeline& timeline, std::vector<double> sliceBeats);
    void setGridBeats(double grid) { gridBeats = grid; }
    void setSelectedBlock(int index);
    // The playhead in beats from the timeline start, or a negative value for none.
    void setPlayheadBeats(double beats);

    // The slice a click on empty row places (the waveform's selected region); -1 = none selected.
    std::function<int()> sliceToPlace;
    // The user asked for an edit: `edit` is applied to a copy of the layer's timeline and published.
    std::function<void(const std::function<void(Timeline&)>& edit)> onEdit;
    // A block was selected (-1 for none).
    std::function<void(int block)> onBlockSelected;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void resized() override { clampView(); }
    bool keyPressed(const juce::KeyPress&) override;

private:
    juce::Rectangle<int> rulerArea() const;
    juce::Rectangle<int> rowArea() const;
    double beatAt(int x) const noexcept;
    int    xOf(double beat) const noexcept;
    // The beats block `i` covers on screen: its own length, else its slice's, cut short by the next block.
    double blockBeats(int i) const;
    int    blockAt(int x) const;
    // True when x is on the right edge of block `i` (where a drag changes its length).
    bool   onRightEdge(int i, int x) const;
    // Keeps the view inside the timeline: at least two beats, at most all of it.
    void   clampView();

    Timeline timeline;
    std::vector<double> sliceBeatsList;
    double gridBeats = 0.25;
    int    selected = -1;
    double playhead = -1.0;
    double viewStart = 0.0;      // the beat at the left edge
    double viewBeats = 0.0;      // how many beats the width shows (0 = the whole timeline)
    double lastLength = 0.0;     // the timeline length the view was last fitted to

    // Drag state: which block, where in it the pointer grabbed it, and whether it copies.
    int    dragBlock = -1;
    double grabOffset = 0.0;
    bool   dragMoved = false;
    bool   resizing = false;     // the drag changes the block's length, not its start

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhraseTimeline)
};

} // namespace mu_tilate
