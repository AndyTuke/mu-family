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

void Timeline::remapSlices(const std::vector<int>& map)
{
    for (auto& b : list) b.slice = b.slice >= 0 && b.slice < (int) map.size() ? map[(size_t) b.slice] : -1;
}

void Timeline::setSlice(int index, int slice)
{
    if (index >= 0 && index < size()) list[(size_t) index].slice = std::max(-1, slice);
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

int Timeline::drop(int index, double newStart, bool asCopy, const std::vector<double>& sliceBeats)
{
    if (index < 0 || index >= size() || newStart < 0.0 || newStart >= length) return -1;
    constexpr double kMin = 1.0 / 16.0;
    TimelineBlock m = list[(size_t) index];
    if (! asCopy) list.erase(list.begin() + index);
    m.start = newStart;

    auto naturalWidth = [&](const TimelineBlock& b)
    {
        const double nat = b.slice >= 0 && b.slice < (int) sliceBeats.size() ? sliceBeats[(size_t) b.slice] : 1.0;
        return b.length > 0.0 ? b.length : std::max(kMin, (nat - b.own.trimInBeats) * b.own.retime);
    };
    const double s = m.start, e = s + std::max(kMin, naturalWidth(m));

    // Phase 1: for each block the dropped one overlaps, work out what survives of it (before the drop, after it, or both).
    std::vector<TimelineBlock> result;
    for (int i = 0; i < (int) list.size(); ++i)
    {
        const TimelineBlock& b = list[(size_t) i];
        const double nextStart = i + 1 < (int) list.size() ? list[(size_t) i + 1].start : length;
        const double bEnd = std::min(b.start + naturalWidth(b), nextStart);   // the next block cuts this one
        if (bEnd <= s + 1.0e-9 || b.start >= e - 1.0e-9) { result.push_back(b); continue; }   // no overlap

        if (b.start < s - 1.0e-9)   // the part before the drop stays, shortened
        {
            TimelineBlock head = b;
            head.length = s - b.start;
            if (head.length >= kMin) result.push_back(head);
        }
        if (bEnd > e + 1.0e-6 && e < length)   // the part after the drop continues from where the dropped block ends
        {
            TimelineBlock tail = b;
            const double cut = e - b.start;
            tail.start = e;
            tail.own.trimInBeats = (float) (b.own.trimInBeats + cut / std::max(0.25f, b.own.retime));
            tail.length = b.length > 0.0 ? b.length - cut : 0.0;
            if (bEnd - e >= kMin && (tail.length == 0.0 || tail.length >= kMin)) result.push_back(tail);
        }
    }
    result.push_back(m);
    std::sort(result.begin(), result.end(), [](const TimelineBlock& x, const TimelineBlock& y) { return x.start < y.start; });
    list = std::move(result);

    for (int i = 0; i < size(); ++i)
        if (std::abs(list[(size_t) i].start - s) < 1.0e-9 && list[(size_t) i].slice == m.slice) return i;
    return -1;
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

int Timeline::trimStart(int index, double newStart, double naturalBeats)
{
    if (index < 0 || index >= size()) return -1;
    TimelineBlock b = list[(size_t) index];
    constexpr double kMin = 1.0 / 16.0;
    const double rt = b.own.retime, trimIn = b.own.trimInBeats;
    const double span = std::max(kMin, (naturalBeats - trimIn) * rt);   // the block's natural width in beats
    const double s = std::clamp(newStart, std::max(0.0, b.start - trimIn * rt), std::min(length - 1.0e-6, b.start + span - kMin));
    for (int i = 0; i < size(); ++i)
        if (i != index && std::abs(list[(size_t) i].start - s) < 1.0e-9) return -1;
    const double d = s - b.start;
    b.own.trimInBeats = (float) std::max(0.0, trimIn + d / rt);
    if (b.length > 0.0) b.length = std::max(kMin, b.length - d);
    b.start = s;
    list.erase(list.begin() + index);
    return insert(b);
}

int Timeline::retime(int index, double newStart, double newEnd, double naturalBeats)
{
    if (index < 0 || index >= size()) return -1;
    TimelineBlock b = list[(size_t) index];
    constexpr double kMin = 1.0 / 16.0;
    const double remaining = std::max(kMin, naturalBeats - b.own.trimInBeats);
    const double oldStart = b.start;
    const double rt = std::clamp((newEnd - newStart) / remaining, 0.25, 4.0);
    const double len = rt * remaining;
    const double s = newStart != oldStart ? newEnd - len : newStart;   // a start-edge drag holds the end; an end-edge drag holds the start
    if (s < 0.0 || s >= length) return -1;
    for (int i = 0; i < size(); ++i)
        if (i != index && std::abs(list[(size_t) i].start - s) < 1.0e-9) return -1;
    b.own.retime = (float) rt;
    b.length = len;
    b.start = s;
    list.erase(list.begin() + index);
    return insert(b);
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
