#pragma once

#include "Sequencer/Timeline.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

// The Phrase layer's timeline view (docs/mu-tilate/design-ui.md, "Timeline"): a ruler in bars and beats over one row of slice
// blocks. It shows the Timeline it is given and reports what the user asked for; it edits nothing itself. A click on empty row
// asks to place the slice the host names, a drag moves a block (Alt = free, Ctrl = copy), Delete removes the selected block.
// The whole timeline is shown (zoom and scroll for long ones come next).
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
    bool keyPressed(const juce::KeyPress&) override;

private:
    juce::Rectangle<int> rulerArea() const;
    juce::Rectangle<int> rowArea() const;
    double beatAt(int x) const noexcept;
    int    xOf(double beat) const noexcept;
    // The beats block `i` covers on screen: its own length, else its slice's, cut short by the next block.
    double blockBeats(int i) const;
    int    blockAt(int x) const;

    Timeline timeline;
    std::vector<double> sliceBeatsList;
    double gridBeats = 0.25;
    int    selected = -1;
    double playhead = -1.0;

    // Drag state: which block, where in it the pointer grabbed it, and whether it copies.
    int    dragBlock = -1;
    double grabOffset = 0.0;
    bool   dragMoved = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhraseTimeline)
};

} // namespace mu_tilate
