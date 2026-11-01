#include "Sequencer/Timeline.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace mu_tilate
{

void Timeline::setLengthBeats(double beats)
{
    length = std::clamp(beats, kMinLengthBeats, kMaxLengthBeats);
    list.erase(std::remove_if(list.begin(), list.end(), [this](const TimelineBlock& b) { return b.start >= length; }), list.end());
}

double Timeline::snap(double beat, double grid) noexcept
{
    return grid > 0.0 ? std::round(beat / grid) * grid : beat;
}

int Timeline::indexAt(double beat) const noexcept
{
    int found = -1;
    for (int i = 0; i < size(); ++i)
    {
        if (list[(size_t) i].start <= beat + 1.0e-9) found = i;
        else break;
    }
    return found;
}

int Timeline::insert(TimelineBlock b)
{
    // A block on an occupied start replaces it; otherwise it goes in start order.
    for (auto& o : list)
        if (std::abs(o.start - b.start) < 1.0e-9) { o = b; return (int) (&o - list.data()); }
    auto it = std::upper_bound(list.begin(), list.end(), b, [](const TimelineBlock& x, const TimelineBlock& y) { return x.start < y.start; });
    return (int) (list.insert(it, b) - list.begin());
}

int Timeline::place(int slice, double beat, double grid, const SliceSettings& own)
{
    const double s = snap(beat, grid);
    if (slice < 0 || s < 0.0 || s >= length) return -1;
    TimelineBlock b;
    b.start = s; b.slice = slice; b.own = sanitised(own);
    return insert(b);
}

int Timeline::move(int index, double beat, double grid)
{
    if (index < 0 || index >= size()) return -1;
    const double s = snap(beat, grid);
    if (s < 0.0 || s >= length) return -1;
    TimelineBlock b = list[(size_t) index];
    list.erase(list.begin() + index);
    b.start = s;
    return insert(b);
}

int Timeline::copy(int index, double beat, double grid)
{
    if (index < 0 || index >= size()) return -1;
    const double s = snap(beat, grid);
    if (s < 0.0 || s >= length) return -1;
    TimelineBlock b = list[(size_t) index];
    b.start = s;
    return insert(b);
}

bool Timeline::setBlockLength(int index, double beats, double grid)
{
    if (index < 0 || index >= size()) return false;
    list[(size_t) index].length = beats <= 0.0 ? 0.0 : std::max(grid > 0.0 ? grid : 1.0 / 64.0, snap(beats, grid));
    return true;
}

void Timeline::setReverse(int index, bool reverse)
{
    if (index >= 0 && index < size()) list[(size_t) index].own.reverse = reverse;
}

void Timeline::setSettings(int index, const SliceSettings& own)
{
    if (index >= 0 && index < size()) list[(size_t) index].own = sanitised(own);
}

bool Timeline::remove(int index)
{
    if (index < 0 || index >= size()) return false;
    list.erase(list.begin() + index);
    return true;
}

void Timeline::sliceInserted(int at)
{
    for (auto& b : list)
        if (b.slice >= at) ++b.slice;
}

void Timeline::sliceRemoved(int at)
{
    for (auto& b : list)
    {
        if (b.slice == at) b.slice = std::max(0, at - 1);
        else if (b.slice > at) --b.slice;
    }
}

void Timeline::originalOrder(const std::vector<double>& sliceBeats, const std::vector<SliceSettings>& sliceSettings)
{
    list.clear();
    double at = 0.0;
    for (int i = 0; i < (int) sliceBeats.size() && at < length; ++i)
    {
        TimelineBlock b;
        b.start = at; b.slice = i;
        if (i < (int) sliceSettings.size()) b.own = sanitised(sliceSettings[(size_t) i]);
        list.push_back(b);
        at += std::max(1.0e-6, sliceBeats[(size_t) i]);
    }
}

void Timeline::randomise(const std::vector<double>& sliceBeats, juce::Random& rng, const std::vector<SliceSettings>& sliceSettings)
{
    // Fisher-Yates over the slice order, then the same back-to-back layout with each slice's own duration.
    std::vector<int> order(sliceBeats.size());
    std::iota(order.begin(), order.end(), 0);
    for (int i = (int) order.size() - 1; i > 0; --i)
        std::swap(order[(size_t) i], order[(size_t) rng.nextInt(i + 1)]);

    list.clear();
    double at = 0.0;
    for (int slice : order)
    {
        if (at >= length) break;
        TimelineBlock b;
        b.start = at; b.slice = slice;
        if (slice < (int) sliceSettings.size()) b.own = sanitised(sliceSettings[(size_t) slice]);
        list.push_back(b);
        at += std::max(1.0e-6, sliceBeats[(size_t) slice]);
    }
}

} // namespace mu_tilate
