#include "Sequencer/SliceList.h"

#include <algorithm>

namespace mu_tilate
{

SliceRange SliceList::slice(int i) const noexcept
{
    if (i < 0 || i >= size()) return {};
    const int start = markers[(size_t) i].position;
    const int end   = i + 1 < size() ? markers[(size_t) i + 1].position : length;
    return { start, end };
}

int SliceList::indexAt(int pos) const noexcept
{
    if (markers.empty() || pos < 0 || pos >= length) return -1;
    // The last marker at or before pos.
    int lo = 0, hi = size() - 1;
    while (lo < hi)
    {
        const int mid = (lo + hi + 1) / 2;
        if (markers[(size_t) mid].position <= pos) lo = mid; else hi = mid - 1;
    }
    return lo;
}

void SliceList::rebuild(std::vector<SliceMarker> m)
{
    // Sorted, unique, inside the loop, and the first marker at 0 so the slices cover the loop.
    std::sort(m.begin(), m.end(), [](const SliceMarker& a, const SliceMarker& b) { return a.position < b.position; });
    std::vector<SliceMarker> out;
    for (const auto& k : m)
    {
        if (k.position < 0 || k.position >= length) continue;
        if (! out.empty() && out.back().position == k.position) { out.back().pinned = out.back().pinned || k.pinned; continue; }
        out.push_back(k);
    }
    if (length > 0 && (out.empty() || out.front().position != 0)) out.insert(out.begin(), SliceMarker { 0, false });
    markers = std::move(out);
}

void SliceList::applyDetected(const std::vector<int>& detected, int minLength, int carryWithin)
{
    const auto before = markers;
    // Phase 1: keep the pinned markers (and the one at 0), then add each detected position that is not too close to a kept one.
    std::vector<SliceMarker> kept;
    for (const auto& k : markers)
        if (k.pinned || k.position == 0) kept.push_back(k);
    for (int p : detected)
    {
        if (p <= 0 || p >= length) continue;
        bool clear = true;
        for (const auto& k : kept)
            if (std::abs(k.position - p) < minLength) { clear = false; break; }
        if (clear) kept.push_back({ p, false });
    }
    rebuild(std::move(kept));
    carrySettings(before, carryWithin);
}

void SliceList::divideEvenly(int parts, int carryWithin)
{
    const auto before = markers;
    std::vector<SliceMarker> kept;
    for (const auto& k : markers)
        if (k.pinned) kept.push_back(k);
    parts = std::max(1, parts);
    for (int i = 0; i < parts; ++i)
        kept.push_back({ (int) ((long long) length * i / parts), false });
    rebuild(std::move(kept));
    carrySettings(before, carryWithin);
}

void SliceList::carrySettings(const std::vector<SliceMarker>& before, int within)
{
    if (within <= 0) return;
    for (auto& m : markers)
    {
        if (m.pinned) continue;   // a kept marker already has its own settings
        const SliceMarker* nearest = nullptr;
        for (const auto& b : before)
            if (std::abs(b.position - m.position) <= within && (nearest == nullptr || std::abs(b.position - m.position) < std::abs(nearest->position - m.position)))
                nearest = &b;
        if (nearest != nullptr) m.settings = nearest->settings;
    }
}

int SliceList::addMarker(int position, int minLength)
{
    if (position <= 0 || position >= length) return -1;
    for (const auto& k : markers)
        if (std::abs(k.position - position) < minLength) return -1;
    auto m = markers;
    // A hand-placed marker is pinned; the new slice starts out playing like the one it was cut from.
    const int parent = indexAt(position);
    m.push_back({ position, true, parent >= 0 ? markers[(size_t) parent].settings : SliceSettings {} });
    rebuild(std::move(m));
    return indexAt(position);
}

int SliceList::moveMarker(int index, int position, int minLength)
{
    if (index <= 0 || index >= size() || position <= 0 || position >= length) return -1;
    // The marker may not pass or crowd its neighbours.
    const int lo = markers[(size_t) index - 1].position + minLength;
    const int hi = (index + 1 < size() ? markers[(size_t) index + 1].position : length) - minLength;
    if (lo > hi) return -1;
    markers[(size_t) index].position = std::clamp(position, lo, hi);
    markers[(size_t) index].pinned = true;   // moving a marker by hand pins it
    return index;
}

bool SliceList::removeMarker(int index)
{
    if (index <= 0 || index >= size()) return false;   // the marker at 0 is the loop's start
    markers.erase(markers.begin() + index);
    return true;
}

void SliceList::setName(int index, const juce::String& name)
{
    if (index < 0 || index >= size()) return;
    const auto trimmed = name.trim().substring(0, 40);
    markers[(size_t) index].name = trimmed == "Slice " + juce::String(index + 1) ? juce::String() : trimmed;
}

void SliceList::setPinned(int index, bool pinned)
{
    if (index > 0 && index < size()) markers[(size_t) index].pinned = pinned;
}

bool SliceList::setSettings(int index, const SliceSettings& settings)
{
    if (index < 0 || index >= size()) return false;
    markers[(size_t) index].settings = sanitised(settings);
    return true;
}

const SliceSettings& SliceList::settings(int index) const
{
    static const SliceSettings kDefault;
    return index >= 0 && index < size() ? markers[(size_t) index].settings : kDefault;
}

SliceList SliceList::cropped(int from, int to, std::vector<int>& map, int minLength) const
{
    from = std::clamp(from, 0, length);
    to = std::clamp(to, from, length);
    SliceList out(to - from);
    map.assign(markers.size(), -1);

    // Phase 1: each slice's part inside [from, to), moved to the new start of the loop.
    std::vector<SliceMarker> kept;
    std::vector<int> keptFrom;
    for (int i = 0; i < size(); ++i)
    {
        const auto r = slice(i);
        const int a = std::max(r.start, from), b = std::min(r.end, to);
        if (b - a < minLength) continue;
        auto m = markers[(size_t) i];
        m.position = a - from;
        kept.push_back(m);
        keptFrom.push_back(i);
    }
    out.rebuild(kept);

    // Phase 2: where each kept slice ended up (rebuild may have put a default marker at 0 before the first).
    for (size_t k = 0; k < kept.size(); ++k)
    {
        const int at = out.indexAt(kept[k].position);
        if (at >= 0 && out.markers[(size_t) at].position == kept[k].position) map[(size_t) keptFrom[k]] = at;
    }
    return out;
}

void SliceList::setLoopLength(int newLength)
{
    length = std::max(0, newLength);
    rebuild(markers);
}

} // namespace mu_tilate
