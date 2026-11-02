#pragma once

#include "Sequencer/SliceSettings.h"

#include <vector>

namespace juce { class Random; }

// The Phrase layer's timeline (docs/mu-tilate/design-ui.md, "Timeline"): one row of slice blocks on a beat grid,
// played by the layer's one monophonic player. A block starts at its beat and plays its slice until the slice
// ends, its own length runs out, or the next block starts (which cuts it). The model is pure data and edit
// operations; the view and the audio-side scheduling use it as it is.
namespace mu_tilate
{

struct TimelineBlock
{
    double start   = 0.0;    // beats from the start of the timeline
    double length  = 0.0;    // beats the block may sound; 0 = the slice's own length
    int    slice   = 0;      // index into the layer's SliceList
    // This hit's own sound: every block has its own settings, copied from the slice's when it is placed. Two hits of the same
    // slice are two separate sounds, and editing one never changes the other (or the slice they came from).
    SliceSettings own;
};

class Timeline
{
public:
    static constexpr double kMinLengthBeats = 1.0;    // one beat
    static constexpr double kMaxLengthBeats = 256.0;  // 64 bars

    explicit Timeline(double lengthInBeats = 16.0) { setLengthBeats(lengthInBeats); }

    double lengthBeats() const noexcept { return length; }
    // Changes the length (1 beat to 64 bars of 4/4, any value between); blocks that would start beyond the end are dropped.
    void setLengthBeats(double beats);

    const std::vector<TimelineBlock>& blocks() const noexcept { return list; }
    int size() const noexcept { return (int) list.size(); }

    // `grid` is the snap in beats (1 = quarter, 0.25 = sixteenth); 0 means no snapping (Alt held).
    static double snap(double beat, double grid) noexcept;

    // Edits. Each returns the index of the changed block, or -1 when the edit is refused (outside the timeline, a
    // missing index). Blocks keep their start order; two blocks never share a start, so placing on an occupied
    // start replaces that block.
    int  place(int slice, double beat, double grid, const SliceSettings& own = SliceSettings());
    int  move(int index, double beat, double grid);
    int  copy(int index, double beat, double grid);
    bool setBlockLength(int index, double beats, double grid);   // 0 = the slice's own length
    // Edge drags. `naturalBeats` is the block's slice length in beats. trimStart moves the start edge to `newStart` and skips
    // (or gives back) that much of the slice, the end edge staying where it is; retime sets the span to [newStart, newEnd], playing
    // the rest of the slice in that time (a ratio of 0.25..4). Both return the block's new index, or -1 when refused (a collision
    // with another block's start, or outside the timeline).
    int  trimStart(int index, double newStart, double naturalBeats);
    int  retime(int index, double newStart, double newEnd, double naturalBeats);
    void setReverse(int index, bool reverse);                   // the hit's own reverse setting
    void setSettings(int index, const SliceSettings& own);      // replaces the hit's own settings (brought into range)
    bool remove(int index);
    void clear() { list.clear(); }

    // Helpers. `sliceBeats[i]` is slice i's own length in beats at the current tempo.
    // `sliceSettings[i]` (when given) is the settings each hit of slice i starts with.
    void originalOrder(const std::vector<double>& sliceBeats, const std::vector<SliceSettings>& sliceSettings = {});   // the slices back to back, in source order, from beat 0
    void randomise(const std::vector<double>& sliceBeats, juce::Random& rng, const std::vector<SliceSettings>& sliceSettings = {});   // the same layout in a shuffled order

    // The slice list changed under the blocks: a slice was inserted at `at` (blocks naming it or a later slice shift up), or
    // the slice at `at` was removed and merged into the one before it (its blocks move to that slice, later ones shift down).
    void sliceInserted(int at);
    void sliceRemoved(int at);

    // The index of the last block starting at or before `beat`, or -1.
    int indexAt(double beat) const noexcept;

private:
    int insert(TimelineBlock b);

    double length = 16.0;
    std::vector<TimelineBlock> list;   // sorted by start, unique starts
};

} // namespace mu_tilate
