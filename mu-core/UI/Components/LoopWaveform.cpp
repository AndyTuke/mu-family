#include "UI/Components/LoopWaveform.h"
#include "UI/StatusSink.h"

#include "UI/Components/MuLookAndFeel.h"

#include <algorithm>
#include <cmath>

namespace
{
    using LF = MuLookAndFeel;
    constexpr int kGrab = 5;   // pixels either side of a marker that grab it (unscaled)
}

LoopWaveform::LoopWaveform()
{
    nameEditor.setMultiLine(false);
    nameEditor.setSelectAllWhenFocused(true);
    nameEditor.setIndents(2, 0);
    nameEditor.onReturnKey = [this] { endRename(true); };
    nameEditor.onEscapeKey = [this] { endRename(false); };
    nameEditor.onFocusLost = [this] { endRename(true); };
    addChildComponent(nameEditor);
}

juce::Rectangle<int> LoopWaveform::labelOf(int region) const
{
    const int a = std::max(0, xOf((double) markers[(size_t) region].position));
    const int b = region + 1 < (int) markers.size() ? xOf((double) markers[(size_t) region + 1].position) : getWidth();
    return { a + 2, 1, std::max(0, std::min(b, getWidth()) - a - 4), mu_ui::s(16) };
}

void LoopWaveform::beginRename(int region)
{
    if (region < 0 || region >= (int) markers.size()) return;
    renaming = region;
    auto r = labelOf(region);
    r.setWidth(std::max(r.getWidth(), mu_ui::s(90)));
    nameEditor.setBounds(r.withRight(std::min(r.getRight(), getWidth() - 1)));
    nameEditor.setText(markers[(size_t) region].name.isNotEmpty() ? markers[(size_t) region].name : "Slice " + juce::String(region + 1), false);
    nameEditor.setVisible(true);
    nameEditor.grabKeyboardFocus();
    nameEditor.selectAll();
}

void LoopWaveform::endRename(bool commit)
{
    if (renaming < 0) return;
    const int region = renaming;
    renaming = -1;
    const auto text = nameEditor.getText();
    nameEditor.setVisible(false);
    if (commit && onRegionRenamed) onRegionRenamed(region, text);
}

void LoopWaveform::setAudio(std::shared_ptr<const juce::AudioBuffer<float>> newAudio)
{
    audio = std::move(newAudio);
    rangeStart = rangeEnd = 0;   // a new loop plays all of itself
    summaryMin.clear();
    summaryMax.clear();
    if (audio != nullptr && audio->getNumSamples() > 0)
    {
        // Phase 1: one min/max pair per block of the mono mix.
        const int n = audio->getNumSamples();
        const int blocks = (n + kBlock - 1) / kBlock;
        summaryMin.assign((size_t) blocks, 0.0f);
        summaryMax.assign((size_t) blocks, 0.0f);
        const int nch = audio->getNumChannels();
        for (int b = 0; b < blocks; ++b)
        {
            float lo = 1.0e9f, hi = -1.0e9f;
            for (int i = b * kBlock; i < std::min(n, (b + 1) * kBlock); ++i)
            {
                float v = 0.0f;
                for (int c = 0; c < nch; ++c) v += audio->getSample(c, i);
                v /= (float) nch;
                lo = std::min(lo, v); hi = std::max(hi, v);
            }
            summaryMin[(size_t) b] = lo; summaryMax[(size_t) b] = hi;
        }
    }
    showAll();
}

void LoopWaveform::setMarkers(std::vector<Marker> m) { markers = std::move(m); repaint(); }
void LoopWaveform::setSelectedRegion(int i)          { selected = i; repaint(); }
void LoopWaveform::setPlayhead(double s)             { if (s != playhead) { playhead = s; repaint(); } }

void LoopWaveform::showAll()
{
    viewStart = 0.0;
    viewLength = std::max(1, length());
    repaint();
}

void LoopWaveform::clampView() noexcept
{
    const double total = (double) std::max(1, length());
    viewLength = std::clamp(viewLength, std::min(total, 64.0), total);   // at most the whole loop, at least a few samples
    viewStart  = std::clamp(viewStart, 0.0, total - viewLength);
}

juce::Rectangle<int> LoopWaveform::scrollTrack() const
{
    return { 0, getHeight() - mu_ui::s(10), getWidth(), mu_ui::s(10) };
}

juce::Rectangle<int> LoopWaveform::scrollThumb() const
{
    const auto t = scrollTrack();
    const double total = (double) std::max(1, length());
    const int x = (int) std::lround(viewStart / total * t.getWidth());
    const int w = std::max(mu_ui::s(24), (int) std::lround(viewLength / total * t.getWidth()));
    return { std::min(x, t.getWidth() - w), t.getY() + 2, w, t.getHeight() - 4 };
}

juce::Rectangle<int> LoopWaveform::crossOf(int marker) const
{
    const int size = mu_ui::s(12), x = xOf((double) markers[(size_t) marker].position);
    return { x + mu_ui::s(2), getHeight() - size - mu_ui::s(12), size, size };
}

void LoopWaveform::setPlayRange(int start, int end)
{
    if (start == rangeStart && end == rangeEnd) return;
    rangeStart = std::max(0, start);
    rangeEnd = end;
    repaint();
}

int LoopWaveform::footY() const noexcept
{
    return getHeight() - (scrollable() ? mu_ui::s(10) : 0);
}

juce::Rectangle<int> LoopWaveform::arrowOf(int handle) const
{
    const int w = mu_ui::s(10), h = mu_ui::s(12);
    const int x = xOf(handle == 0 ? (double) rangeStart : (double) getPlayEnd());
    return { handle == 0 ? x : x - w + 1, footY() - h, w, h };
}

int LoopWaveform::rangeHandleAt(juce::Point<int> p) const noexcept
{
    if (length() <= 0) return -1;
    // The arrows take the press first; the line itself grabs only where no slice marker is nearer.
    for (int h = 1; h >= 0; --h)
        if (arrowOf(h).expanded(mu_ui::s(2)).contains(p)) return h;
    const int m = markerNear(p.x);
    if (m > 0) return -1;
    int best = -1, bestDist = mu_ui::s(3) + 1;
    for (int h = 0; h < 2; ++h)
    {
        const int d = std::abs(xOf(h == 0 ? (double) rangeStart : (double) getPlayEnd()) - p.x);
        if (d < bestDist) { bestDist = d; best = h; }
    }
    return best;
}

int LoopWaveform::markerNear(int x) const noexcept
{
    int best = -1, bestDist = mu_ui::s(kGrab) + 1;
    for (int i = 0; i < (int) markers.size(); ++i)
    {
        const int d = std::abs(xOf((double) markers[(size_t) i].position) - x);
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return best;
}

int LoopWaveform::regionAt(int sample) const noexcept
{
    int r = -1;
    for (int i = 0; i < (int) markers.size(); ++i)
        if (markers[(size_t) i].position <= sample) r = i;
    return r;
}

void LoopWaveform::paint(juce::Graphics& g)
{
    using mu_ui::s;
    const auto bounds = getLocalBounds();
    g.setColour(LF::colour(LF::lfoEditorBackground));
    g.fillRoundedRectangle(bounds.toFloat(), 3.0f);

    if (length() <= 0 || getWidth() <= 0)
    {
        g.setColour(LF::colour(LF::labelText));
        g.drawText("No loop", bounds, juce::Justification::centred);
        return;
    }

    const float midY = (float) getHeight() * 0.5f;
    const float halfH = (float) getHeight() * 0.46f;
    const double spp = samplesPerPixel();

    // Phase 1: the selected region's tint, under the waveform.
    if (selected >= 0 && selected < (int) markers.size())
    {
        const double a = (double) markers[(size_t) selected].position;
        const double b = selected + 1 < (int) markers.size() ? (double) markers[(size_t) selected + 1].position : (double) length();
        g.setColour(LF::appAccent(*this).withAlpha(0.18f));
        g.fillRect(juce::Rectangle<int>(xOf(a), 0, std::max(1, xOf(b) - xOf(a)), getHeight()));
    }

    // Phase 2: the waveform, one min/max column per pixel (from the summary when zoomed out, from the samples when in).
    g.setColour(LF::colour(LF::lfoEditorCurve));
    const int nch = audio->getNumChannels();
    for (int x = 0; x < getWidth(); ++x)
    {
        const double s0 = viewStart + (double) x * spp, s1 = s0 + spp;
        if (s1 < 0.0 || s0 >= (double) length()) continue;
        float lo = 1.0e9f, hi = -1.0e9f;
        if (spp >= (double) kBlock)
        {
            const int b0 = std::max(0, (int) (s0 / kBlock)), b1 = std::min((int) summaryMin.size() - 1, (int) (s1 / kBlock));
            for (int b = b0; b <= b1; ++b) { lo = std::min(lo, summaryMin[(size_t) b]); hi = std::max(hi, summaryMax[(size_t) b]); }
        }
        else
        {
            const int i0 = std::max(0, (int) s0), i1 = std::min(length() - 1, std::max(i0, (int) s1));
            for (int i = i0; i <= i1; ++i)
            {
                float v = 0.0f;
                for (int c = 0; c < nch; ++c) v += audio->getSample(c, i);
                v /= (float) nch;
                lo = std::min(lo, v); hi = std::max(hi, v);
            }
        }
        if (lo > hi) continue;
        const float y0 = midY - std::clamp(hi, -1.0f, 1.0f) * halfH, y1 = midY - std::clamp(lo, -1.0f, 1.0f) * halfH;
        g.fillRect((float) x, y0, 1.0f, std::max(1.0f, y1 - y0));
    }

    // Phase 3: the region names, then the markers and the playhead.
    g.setFont(MuLookAndFeel::uiFont((float) s(11)));
    for (int i = 0; i < (int) markers.size(); ++i)
    {
        if (i == renaming) continue;
        const auto r = labelOf(i);
        if (r.getWidth() < s(24)) continue;
        g.setColour(LF::colour(LF::labelText).withAlpha(i == selected ? 1.0f : 0.7f));
        g.drawText(markers[(size_t) i].name.isNotEmpty() ? markers[(size_t) i].name : "Slice " + juce::String(i + 1), r, juce::Justification::centredLeft, true);
    }
    for (int i = 0; i < (int) markers.size(); ++i)
    {
        const int x = xOf((double) markers[(size_t) i].position);
        if (x < -2 || x > getWidth() + 2) continue;
        const bool hot = i == hovered || i == dragging;
        g.setColour(hot ? LF::colour(LF::lfoEditorPointHover) : LF::colour(LF::lfoEditorPlayhead).withAlpha(0.85f));
        g.fillRect(x, 0, hot ? 2 : 1, getHeight());
        if (i == hovered && i > 0)
        {
            // The delete cross, at the marker's foot.
            const auto c = crossOf(i).toFloat();
            g.setColour(LF::colour(LF::lfoEditorBackground).withAlpha(0.85f));
            g.fillEllipse(c);
            g.setColour(LF::colour(LF::lfoEditorPointHover));
            const auto k = c.reduced(c.getWidth() * 0.3f);
            g.drawLine(k.getX(), k.getY(), k.getRight(), k.getBottom(), 1.5f);
            g.drawLine(k.getX(), k.getBottom(), k.getRight(), k.getY(), 1.5f);
        }
        if (markers[(size_t) i].pinned)
        {
            g.setColour(LF::colour(LF::lfoEditorPoint));
            g.fillEllipse((float) x - (float) s(3), 1.0f, (float) s(6), (float) s(6));
        }
    }
    // Phase 4: the play area: what lies outside it is dimmed, and each end is a line with an arrow at the foot.
    {
        const int xs = xOf((double) rangeStart), xe = xOf((double) getPlayEnd());
        g.setColour(LF::colour(LF::lfoEditorBackground).withAlpha(0.6f));
        if (xs > 0) g.fillRect(0, 0, std::min(xs, getWidth()), getHeight());
        if (xe < getWidth()) g.fillRect(std::max(0, xe), 0, getWidth() - std::max(0, xe), getHeight());
        const auto accent = LF::appAccent(*this);
        for (int h = 0; h < 2; ++h)
        {
            const int x = h == 0 ? xs : xe;
            if (x < -2 || x > getWidth() + 2) continue;
            const bool hot = draggingRange == h;
            g.setColour(hot ? accent : accent.withAlpha(0.85f));
            g.fillRect(x, 0, hot ? 2 : 1, footY());
            const auto a = arrowOf(h).toFloat();
            juce::Path arrow;
            // A pennant at the foot, pointing into the area it bounds.
            if (h == 0) arrow.addTriangle(a.getX(), a.getY(), a.getX(), a.getBottom(), a.getRight(), a.getCentreY());
            else        arrow.addTriangle(a.getRight(), a.getY(), a.getRight(), a.getBottom(), a.getX(), a.getCentreY());
            g.fillPath(arrow);
        }
    }
    if (playhead >= 0.0)
    {
        g.setColour(LF::colour(LF::lfoEditorPoint));
        g.fillRect(xOf(playhead), 0, 1, getHeight());
    }
    if (scrollable())
    {
        g.setColour(LF::colour(LF::lfoEditorBackground).withAlpha(0.8f));
        g.fillRect(scrollTrack());
        g.setColour(LF::appAccent(*this).withAlpha(scrolling ? 0.9f : 0.6f));
        g.fillRoundedRectangle(scrollThumb().toFloat(), 3.0f);
    }
}

void LoopWaveform::mouseMove(const juce::MouseEvent& e)
{
    // The cross sits beside its marker, so a pointer over it still counts as on that marker.
    if (scrollable() && scrollTrack().contains(e.getPosition()))
    {
        if (hovered >= 0) { hovered = -1; repaint(); }
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }
    if (rangeHandleAt(e.getPosition()) >= 0)
    {
        if (hovered >= 0) { hovered = -1; repaint(); }
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        return;
    }
    int h = markerNear(e.x);
    if (h < 0 && hovered > 0 && hovered < (int) markers.size() && crossOf(hovered).expanded(mu_ui::s(2)).contains(e.getPosition())) h = hovered;
    if (h != hovered) { hovered = h; repaint(); }
    const bool onCross = h > 0 && crossOf(h).contains(e.getPosition());
    setMouseCursor(onCross ? juce::MouseCursor::PointingHandCursor : h > 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void LoopWaveform::mouseDown(const juce::MouseEvent& e)
{
    if (length() <= 0) return;
    if (e.mods.isMiddleButtonDown() || e.mods.isCtrlDown()) { panning = true; panStartView = viewStart; return; }
    if (scrollable() && scrollTrack().contains(e.getPosition()))
    {
        // On the thumb: drag it. On the track: the thumb jumps to be centred there, and is then dragged.
        const double perPixel = (double) length() / (double) std::max(1, getWidth());
        if (! scrollThumb().contains(e.getPosition()))
        {
            viewStart = (double) e.x * perPixel - viewLength * 0.5;
            clampView();
        }
        scrolling = true;
        scrollGrab = (double) e.x * perPixel - viewStart;
        repaint();
        return;
    }
    if (hovered > 0 && hovered < (int) markers.size() && crossOf(hovered).contains(e.getPosition()))
    {
        mu_ui::reportStatus(*this, "Marker", "Removed");
        if (onMarkerDeleteRequested) onMarkerDeleteRequested(hovered);
        hovered = -1;
        return;
    }
    if (! e.mods.isRightButtonDown())
        if (const int h = rangeHandleAt(e.getPosition()); h >= 0) { draggingRange = h; repaint(); return; }
    const int m = markerNear(e.x);
    if (m > 0 && (e.mods.isAltDown()))
    {
        mu_ui::reportStatus(*this, "Marker", "Removed");
        if (onMarkerDeleteRequested) onMarkerDeleteRequested(m);
        return;
    }
    if (m > 0 && ! e.mods.isRightButtonDown()) { dragging = m; return; }       // marker 0 is the loop's start: it stays
    const int r = regionAt(sampleAt(e.x));
    if (e.mods.isRightButtonDown())
    {
        if (r >= 0 && onRegionPlayRequested) onRegionPlayRequested(r);
        return;
    }
    pressedRegion = r;
    regionDragging = false;
    if (r >= 0) mu_ui::reportStatus(*this, "Region", juce::String(r + 1));
    if (r >= 0 && onRegionClicked) onRegionClicked(r);
}

void LoopWaveform::mouseDrag(const juce::MouseEvent& e)
{
    if (scrolling)
    {
        viewStart = (double) e.x * (double) length() / (double) std::max(1, getWidth()) - scrollGrab;
        clampView();
        repaint();
        return;
    }
    if (panning)
    {
        viewStart = panStartView - (double) e.getDistanceFromDragStartX() * samplesPerPixel();
        clampView();
        repaint();
        return;
    }
    if (draggingRange >= 0)
    {
        int pos = sampleAt(e.x);
        // Onto a slice marker when close to one (Shift moves freely).
        if (! e.mods.isShiftDown())
            for (const auto& m : markers)
                if (std::abs(xOf((double) m.position) - e.x) <= mu_ui::s(kGrab)) { pos = m.position; break; }
        const int minSpan = 2 * kBlock;
        if (draggingRange == 0) rangeStart = std::clamp(pos, 0, std::max(0, getPlayEnd() - minSpan));
        else                    rangeEnd = std::clamp(pos, std::min(length(), rangeStart + minSpan), length());
        repaint();
        mu_ui::reportStatus(*this, draggingRange == 0 ? "Play Range Start" : "Play Range End", juce::String(draggingRange == 0 ? rangeStart : getPlayEnd()) + " samples");
        if (onPlayRangeChanged) onPlayRangeChanged(rangeStart, getPlayEnd());
        return;
    }
    if (pressedRegion >= 0 && dragging < 0)
    {
        // A press inside a region that moves far enough is a drag of that region (to the timeline), not a click.
        if (! regionDragging && e.getDistanceFromDragStart() > mu_ui::s(6)) regionDragging = true;
        if (regionDragging && onRegionDragged) onRegionDragged(pressedRegion, e.getScreenPosition());
        return;
    }
    if (dragging < 0) return;
    int pos = sampleAt(e.x);
    if (snapMarker && ! e.mods.isShiftDown()) pos = snapMarker(pos, samplesPerPixel());
    pos = std::clamp(pos, 1, std::max(1, length() - 1));
    mu_ui::reportStatus(*this, "Marker " + juce::String(dragging), juce::String(pos) + " samples");
    if (onMarkerMoved) onMarkerMoved(dragging, pos);
}

void LoopWaveform::mouseUp(const juce::MouseEvent& e)
{
    if (regionDragging && pressedRegion >= 0 && onRegionDropped) onRegionDropped(pressedRegion, e.getScreenPosition());
    pressedRegion = -1;
    regionDragging = false;
    panning = false;
    scrolling = false;
    dragging = -1;
    draggingRange = -1;
    repaint();
}

void LoopWaveform::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (length() <= 0 || markerNear(e.x) >= 0 || rangeHandleAt(e.getPosition()) >= 0) return;
    // A double-click on a region's name strip renames it; elsewhere it adds a marker.
    for (int i = 0; i < (int) markers.size(); ++i)
        if (labelOf(i).contains(e.getPosition())) { beginRename(i); return; }
    int pos = sampleAt(e.x);
    if (snapNewMarker) pos = snapNewMarker(pos);
    mu_ui::reportStatus(*this, "Marker", "Added");
    if (onMarkerAddRequested) onMarkerAddRequested(std::clamp(pos, 1, std::max(1, length() - 1)));
}

void LoopWaveform::zoomBy(double factor, int anchorX)
{
    if (length() <= 0 || factor <= 0.0) return;
    // The sample under the anchor stays under it.
    const double at = viewStart + (double) anchorX * samplesPerPixel();
    viewLength /= factor;
    clampView();
    viewStart = at - (double) anchorX * samplesPerPixel();
    clampView();
    repaint();
}

void LoopWaveform::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (length() <= 0) return;
    const bool horizontal = e.mods.isShiftDown() || std::abs(w.deltaX) > std::abs(w.deltaY);
    const float d = horizontal && std::abs(w.deltaX) > std::abs(w.deltaY) ? w.deltaX : w.deltaY;
    if (horizontal)
    {
        viewStart -= (double) d * viewLength * 0.25;   // scroll a quarter of the view per notch
        clampView();
        repaint();
    }
    else
        zoomBy(std::pow(1.25, (double) d * 5.0), e.x);   // the wheel zooms around the pointer
}
