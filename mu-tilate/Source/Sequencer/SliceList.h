#pragma once

#include "Audio/SlicePlayer.h"   // SliceRange
#include "Sequencer/SliceSettings.h"

#include <juce_core/juce_core.h>

#include <vector>

// The slices of one loop (docs/mu-tilate/design.md): ordered start markers that cover the loop, each slice
// running from its marker to the next one (the last to the end of the loop). Detection supplies the markers; the
// user can move, add, delete or pin them (a pinned marker survives a re-detect).
namespace mu_tilate
{

struct SliceMarker
{
    int  position = 0;     // source samples
    bool pinned   = false; // a hand-placed marker: kept when detection runs again
    SliceSettings settings; // how the slice that starts here plays
    juce::String name;      // the user's name for the slice; empty = the default "Slice N"
};

class SliceList
{
public:
    // The first marker is always at 0, so the slices cover the whole loop.
    explicit SliceList(int loopLength = 0) : length(loopLength) { rebuild({}); }

    int  loopLength() const noexcept { return length; }
    int  size() const noexcept { return (int) markers.size(); }
    const SliceMarker& marker(int i) const { return markers[(size_t) i]; }

    // Slice i as [start, end); an out-of-range index is an empty slice.
    SliceRange slice(int i) const noexcept;
    // The slice that holds source position `pos` (-1 for an empty list or a position outside the loop).
    int indexAt(int pos) const noexcept;

    // Replaces the unpinned markers with `detected` (source samples). Pinned markers stay where they are. Positions
    // outside the loop, duplicates and positions closer than minLength to a neighbour are dropped. A new marker within
    // `carryWithin` samples of a marker it replaces takes that marker's slice settings (0 = none carried).
    void applyDetected(const std::vector<int>& detected, int minLength = 32, int carryWithin = 0);
    // Evenly divides the loop into `parts` slices (the "Even divisions" method); pinned markers are kept; settings carry as above.
    void divideEvenly(int parts, int carryWithin = 0);

    // Manual editing. Each returns the index of the changed marker, or -1 when the edit is refused (a marker too
    // close to a neighbour, outside the loop, the first marker, or a missing index).
    int  addMarker(int position, int minLength = 32);
    int  moveMarker(int index, int position, int minLength = 32);
    bool removeMarker(int index);
    void setPinned(int index, bool pinned);
    // The settings of slice `index`; an out-of-range index is refused (returns false) and changes nothing.
    bool setSettings(int index, const SliceSettings& settings);
    const SliceSettings& settings(int index) const;
    // The slice's name (empty = default). A name equal to the default is stored as empty.
    void setName(int index, const juce::String& name);
    juce::String displayName(int index) const { return index >= 0 && index < size() && marker(index).name.isNotEmpty() ? marker(index).name : "Slice " + juce::String(index + 1); }

    // The loop changed length (a new loop): the markers outside it are dropped.
    void setLoopLength(int newLength);

private:
    void rebuild(std::vector<SliceMarker> m);
    // Gives each marker that has default settings the settings of the nearest marker in `before` within `within` samples.
    void carrySettings(const std::vector<SliceMarker>& before, int within);

    int length = 0;
    std::vector<SliceMarker> markers;   // sorted, markers[0].position == 0 once there is a loop
};

} // namespace mu_tilate
