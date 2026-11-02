#pragma once

#include "Sequencer/Timeline.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

// The Phrase layer's timeline view (docs/mu-tilate/design-ui.md, "Timeline"): a ruler in bars and beats over one row of slice
// blocks. It shows the Timeline it is given and reports what the user asked for; it edits nothing itself. A click on empty row
// asks to place the slice the host names, a drag moves a block (Shift = free, Ctrl = copy), Delete removes the selected block.
// The view is zoomed and scrolled: the wheel zooms around the pointer, Shift+wheel (or a horizontal wheel) scrolls, a middle-button
// drag or a drag on the ruler pans, and while it plays the view pages on to keep the playhead in sight. Dragging a block's edge trims it (Alt retimes it instead: the slice is stretched or pitched to the new width; Shift = free, else the grid).
namespace mu_tilate
{

class PhraseTimeline : public juce::Component
{
public:
    PhraseTimeline() { setWantsKeyboardFocus(true); }

    // The timeline to show, how long each slice is in beats (index = slice), the snap grid in beats (0 = none) and the selected block.
    void setTimeline(const Timeline& timeline, std::vector<double> sliceBeats);
    void setGridBeats(double grid) { gridBeats = grid; }
    // The shape of each slice's sound (index = slice): columns of peak level 0..1, drawn inside its blocks and in the drop preview.
    void setSlicePeaks(std::vector<std::vector<float>> peaks) { slicePeaksList = std::move(peaks); repaint(); }
    // The whole loop's peaks and where each slice sits in it (start and length as fractions 0..1), so a block longer than its slice
    // can show the audio it runs on into.
    struct SliceSpan { double start = 0.0, length = 0.0; };
    void setLoopPeaks(std::vector<float> peaks, std::vector<SliceSpan> spans) { loopPeaks = std::move(peaks); sliceSpans = std::move(spans); repaint(); }
    // The slice being dragged over the timeline (-1 for none): the drop preview shows its waveform at the drop beat.
    void setDropSlice(int slice) { if (slice != dropSlice) { dropSlice = slice; repaint(); } }
    // How wide a slice is on screen at the current zoom, in pixels.
    int  pixelsForSlice(int slice) const;
    void setSelectedBlock(int index);
    int  getSelectedBlock() const noexcept { return selected; }
    // Zoom by `factor` (above 1 zooms in) around the beat at `anchorX` pixels; showAll() fits the whole timeline again.
    void zoomBy(double factor, int anchorX);
    void showAll() { viewBeats = 0.0; viewStart = 0.0; repaint(); }
    // A drop in progress: shows an insertion line at this beat (negative for none). dropBeatAt turns a point in this
    // component's coordinates into the beat a block dropped there would start on (snapped unless free), or a negative
    // value when the point is outside the row.
    void   setDropBeat(double beat) { if (beat != dropBeat) { dropBeat = beat; repaint(); } }
    double dropBeatAt(juce::Point<int> p, bool free) const;
    // The playhead in beats from the timeline start, or a negative value for none.
    void setPlayheadBeats(double beats);

    // The slice a click on empty row places (the waveform's selected region); -1 = none selected.
    std::function<int()> sliceToPlace;
    // The settings a new hit of this slice starts with (the slice's own); a hit is a separate sound from then on.
    std::function<SliceSettings(int slice)> settingsOfSlice;
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
    // The block with an edge under x (`right` says which), or -1.
    int    edgeAt(int x, bool& right) const;
    double naturalBeatsOf(int i) const;   // the block's whole slice, in beats
    // Keeps the view inside the timeline: at least two beats, at most all of it.
    void   clampView();

    Timeline timeline;
    Timeline dragOrigin;   // the timeline as it was when a block drag began: each step is worked out from it, so passing over a block loses nothing
    std::vector<double> sliceBeatsList;
    std::vector<std::vector<float>> slicePeaksList;
    std::vector<float> loopPeaks;
    std::vector<SliceSpan> sliceSpans;
    int    dropSlice = -1;       // the slice a drop in progress would place
    double gridBeats = 0.25;
    int    selected = -1;
    double playhead = -1.0;
    double viewStart = 0.0;      // the beat at the left edge
    double viewBeats = 0.0;      // how many beats the width shows (0 = the whole timeline)
    double lastLength = 0.0;     // the timeline length the view was last fitted to
    double dropBeat = -1.0;      // where a drop in progress would land
    bool   panning = false;      // a middle-button or ruler drag is moving the view
    double panStartBeat = 0.0;

    // Drag state: which block, where in it the pointer grabbed it, and whether it copies.
    int    dragBlock = -1;
    int    dragOriginIndex = -1;
    double lastDropStart = 0.0;   // where the dragged block was last put (the view's timeline may be a step behind)
    double grabOffset = 0.0;
    bool   dragMoved = false;
    enum class Edge { None, Left, Right };
    Edge   dragEdge = Edge::None;   // the drag is on a block edge (trim, or with Alt retime), not moving the block
    double dragAnchor = 0.0;        // the edge that stays put during an edge drag, in beats

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhraseTimeline)
};

} // namespace mu_tilate
