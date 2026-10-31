#pragma once

#include "Audio/SlicePlayer.h"   // SliceRange

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
    // outside the loop, duplicates and positions closer than minLength to a neighbour are dropped.
    void applyDetected(const std::vector<int>& detected, int minLength = 32);
    // Evenly divides the loop into `parts` slices (the "Even divisions" method); pinned markers are kept.
    void divideEvenly(int parts);

    // Manual editing. Each returns the index of the changed marker, or -1 when the edit is refused (a marker too
    // close to a neighbour, outside the loop, the first marker, or a missing index).
    int  addMarker(int position, int minLength = 32);
    int  moveMarker(int index, int position, int minLength = 32);
    bool removeMarker(int index);
    void setPinned(int index, bool pinned);

    // The loop changed length (a new loop): the markers outside it are dropped.
    void setLoopLength(int newLength);

private:
    void rebuild(std::vector<SliceMarker> m);

    int length = 0;
    std::vector<SliceMarker> markers;   // sorted, markers[0].position == 0 once there is a loop
};

} // namespace mu_tilate
