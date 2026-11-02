#include "UI/Components/LoopWaveform.h"

#include "UI/Components/MuLookAndFeel.h"

#include <algorithm>
#include <cmath>

namespace
{
    using LF = MuLookAndFeel;
    constexpr int kGrab = 5;   // pixels either side of a marker that grab it (unscaled)
}

void LoopWaveform::setAudio(std::shared_ptr<const juce::AudioBuffer<float>> newAudio)
{
    audio = std::move(newAudio);
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

    // Phase 3: the markers, then the playhead.
    for (int i = 0; i < (int) markers.size(); ++i)
    {
        const int x = xOf((double) markers[(size_t) i].position);
        if (x < -2 || x > getWidth() + 2) continue;
        const bool hot = i == hovered || i == dragging;
        g.setColour(hot ? LF::colour(LF::lfoEditorPointHover) : LF::colour(LF::lfoEditorPlayhead).withAlpha(0.85f));
        g.fillRect(x, 0, hot ? 2 : 1, getHeight());
        if (markers[(size_t) i].pinned)
        {
            g.setColour(LF::colour(LF::lfoEditorPoint));
            g.fillEllipse((float) x - (float) s(3), 1.0f, (float) s(6), (float) s(6));
        }
    }
    if (playhead >= 0.0)
    {
        g.setColour(LF::colour(LF::lfoEditorPoint));
        g.fillRect(xOf(playhead), 0, 1, getHeight());
    }
}

void LoopWaveform::mouseMove(const juce::MouseEvent& e)
{
    const int h = markerNear(e.x);
    if (h != hovered) { hovered = h; repaint(); }
    setMouseCursor(h > 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void LoopWaveform::mouseDown(const juce::MouseEvent& e)
{
    if (length() <= 0) return;
    if (e.mods.isMiddleButtonDown() || e.mods.isCtrlDown()) { panning = true; panStartView = viewStart; return; }
    const int m = markerNear(e.x);
    if (m > 0 && (e.mods.isAltDown() || e.mods.isRightButtonDown()))
    {
        if (onMarkerDeleteRequested) onMarkerDeleteRequested(m);
        return;
    }
    if (m > 0) { dragging = m; return; }       // marker 0 is the loop's start: it stays
    const int r = regionAt(sampleAt(e.x));
    if (e.mods.isRightButtonDown())
    {
        if (r >= 0 && onRegionPlayRequested) onRegionPlayRequested(r);
        return;
    }
    pressedRegion = r;
    regionDragging = false;
    if (r >= 0 && onRegionClicked) onRegionClicked(r);
}

void LoopWaveform::mouseDrag(const juce::MouseEvent& e)
{
    if (panning)
    {
        viewStart = panStartView - (double) e.getDistanceFromDragStartX() * samplesPerPixel();
        clampView();
        repaint();
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
    if (onMarkerMoved) onMarkerMoved(dragging, pos);
}

void LoopWaveform::mouseUp(const juce::MouseEvent& e)
{
    if (regionDragging && pressedRegion >= 0 && onRegionDropped) onRegionDropped(pressedRegion, e.getScreenPosition());
    pressedRegion = -1;
    regionDragging = false;
    panning = false;
    dragging = -1;
    repaint();
}

void LoopWaveform::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (length() <= 0 || markerNear(e.x) >= 0) return;
    if (onMarkerAddRequested) onMarkerAddRequested(std::clamp(sampleAt(e.x), 1, std::max(1, length() - 1)));
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
