#include "UI/PhraseTimeline.h"

#include "UI/Components/MuLookAndFeel.h"

#include <algorithm>
#include <cmath>

namespace
{
    using LF = MuLookAndFeel;
    constexpr int kRulerH = 18;   // unscaled
}

namespace mu_tilate
{

void PhraseTimeline::setTimeline(const Timeline& t, std::vector<double> beats)
{
    timeline = t;
    sliceBeatsList = std::move(beats);
    if (selected >= timeline.size()) selected = -1;
    if (dragBlock >= timeline.size()) { dragBlock = -1; dragEdge = Edge::None; }   // the timeline shrank under a drag
    if (timeline.lengthBeats() != lastLength) { lastLength = timeline.lengthBeats(); viewBeats = 0.0; viewStart = 0.0; }   // a new length: show all of it
    clampView();
    repaint();
}

void PhraseTimeline::setSelectedBlock(int i)   { selected = i; repaint(); }
void PhraseTimeline::setPlayheadBeats(double b)
{
    if (b == playhead) return;
    const double before = playhead;
    playhead = b;

    // While playing, page the view on when the playhead walks out of it (or starts outside it). A playhead that was already
    // outside, because the user scrolled away, leaves the view alone.
    const double L = timeline.lengthBeats();
    if (playhead >= 0.0 && viewBeats > 0.0 && viewBeats < L - 1.0e-9)
    {
        auto inside = [&](double beat) { const double p = std::fmod(beat, L); return p >= viewStart && p < viewStart + viewBeats; };
        if (! inside(playhead) && (before < 0.0 || inside(before)))
        {
            viewStart = std::floor(std::fmod(playhead, L) / viewBeats) * viewBeats;
            clampView();
        }
    }
    repaint();
}

void PhraseTimeline::zoomBy(double factor, int anchorX)
{
    const double L = timeline.lengthBeats();
    if (L <= 0.0 || factor <= 0.0) return;
    const double shown = viewBeats > 0.0 ? viewBeats : L;
    const double anchor = beatAt(anchorX);
    const double frac = (double) anchorX / (double) std::max(1, getWidth());
    viewBeats = shown / factor;
    viewStart = anchor - frac * viewBeats;
    clampView();
    repaint();
}

double PhraseTimeline::dropBeatAt(juce::Point<int> p, bool free) const
{
    if (! rowArea().contains(p)) return -1.0;
    const double beat = free ? beatAt(p.x) : Timeline::snap(beatAt(p.x), gridBeats);
    return beat >= 0.0 && beat < timeline.lengthBeats() ? beat : -1.0;
}

void PhraseTimeline::clampView()
{
    const double L = std::max(1.0e-6, timeline.lengthBeats());
    if (viewBeats <= 0.0 || viewBeats >= L) { viewBeats = 0.0; viewStart = 0.0; return; }
    viewBeats = std::max(std::min(2.0, L), viewBeats);
    viewStart = std::clamp(viewStart, 0.0, L - viewBeats);
}

juce::Rectangle<int> PhraseTimeline::rulerArea() const { return getLocalBounds().removeFromTop(mu_ui::s(kRulerH)); }
juce::Rectangle<int> PhraseTimeline::rowArea() const   { auto r = getLocalBounds(); r.removeFromTop(mu_ui::s(kRulerH)); return r; }

int PhraseTimeline::xOf(double beat) const noexcept
{
    const double shown = viewBeats > 0.0 ? viewBeats : timeline.lengthBeats();
    return (int) std::lround((beat - viewStart) / std::max(1.0e-6, shown) * (double) getWidth());
}

double PhraseTimeline::beatAt(int x) const noexcept
{
    const double shown = viewBeats > 0.0 ? viewBeats : timeline.lengthBeats();
    return viewStart + (double) x / (double) std::max(1, getWidth()) * shown;
}

double PhraseTimeline::naturalBeatsOf(int i) const
{
    const auto& b = timeline.blocks()[(size_t) i];
    return b.slice >= 0 && b.slice < (int) sliceBeatsList.size() ? sliceBeatsList[(size_t) b.slice] : 1.0;
}

int PhraseTimeline::edgeAt(int x, bool& right) const
{
    int best = -1, bestDist = mu_ui::s(LF::kEdgeGrab) + 1;
    for (int i = 0; i < timeline.size(); ++i)
    {
        const double start = timeline.blocks()[(size_t) i].start;
        const int dl = std::abs(x - xOf(start)), dr = std::abs(x - xOf(start + blockBeats(i)));
        if (dl < bestDist) { best = i; bestDist = dl; right = false; }
        if (dr < bestDist) { best = i; bestDist = dr; right = true; }
    }
    return best;
}

juce::Rectangle<int> PhraseTimeline::blockRect(int i) const
{
    const auto row = rowArea();
    const auto& b = timeline.blocks()[(size_t) i];
    const int x0 = xOf(b.start), x1 = xOf(b.start + blockBeats(i));
    return { x0, row.getY() + mu_ui::s(LF::kBlockInset), std::max(2, x1 - x0 - 1), row.getHeight() - mu_ui::s(2 * LF::kBlockInset) };
}

int PhraseTimeline::levelHandleAt(juce::Point<int> p) const
{
    // The line sits at the hit's volume up from the foot of the block; a few pixels either side of it grab it.
    for (int i = 0; i < timeline.size(); ++i)
    {
        const auto r = blockRect(i);
        const int y = r.getBottom() - (int) std::lround(timeline.blocks()[(size_t) i].own.volume * (float) r.getHeight());
        if (r.contains(p.x, p.y) && std::abs(p.y - y) <= mu_ui::s(5)) return i;
    }
    return -1;
}

double PhraseTimeline::blockBeats(int i) const
{
    const auto& b = timeline.blocks()[(size_t) i];
    double len = b.length > 0.0 ? b.length : std::max(0.0, naturalBeatsOf(i) - b.own.trimInBeats) * b.own.retime;
    const double end = i + 1 < timeline.size() ? timeline.blocks()[(size_t) i + 1].start : timeline.lengthBeats();
    return std::max(0.0, std::min(len, end - b.start));   // the next block cuts this one
}

int PhraseTimeline::blockAt(int x) const
{
    const double beat = beatAt(x);
    for (int i = 0; i < timeline.size(); ++i)
        if (beat >= timeline.blocks()[(size_t) i].start && beat < timeline.blocks()[(size_t) i].start + blockBeats(i)) return i;
    return -1;
}

namespace
{
    // The part [from, to] (0..1) of a slice's waveform as mirrored bars across `r`, one pixel wide, from its columns of peak level
    // (reversed when the hit plays backwards).
    void drawPeaks(juce::Graphics& g, const std::vector<float>& peaks, juce::Rectangle<int> r, bool reversed, int clipLeft, int clipRight,
                   double from = 0.0, double to = 1.0)
    {
        if (peaks.empty() || r.getWidth() < 2) return;
        const int n = (int) peaks.size(), mid = r.getCentreY(), half = r.getHeight() / 2 - 2;
        for (int x = std::max(r.getX(), clipLeft); x < std::min(r.getRight(), clipRight); ++x)
        {
            double t = from + (to - from) * (double) (x - r.getX()) / (double) r.getWidth();
            if (reversed) t = to - (t - from);   // mirrored inside the part shown
            const int i = std::clamp((int) (t * n), 0, n - 1);
            const int h = std::max(1, (int) std::lround(peaks[(size_t) i] * (float) half));
            g.fillRect(x, mid - h, 1, h * 2);
        }
    }
}

int PhraseTimeline::pixelsForSlice(int slice) const
{
    if (slice < 0 || slice >= (int) sliceBeatsList.size()) return 0;
    return std::max(2, xOf(sliceBeatsList[(size_t) slice]) - xOf(0.0));
}

void PhraseTimeline::paint(juce::Graphics& g)
{
    using mu_ui::s;
    g.setColour(LF::colour(LF::lfoEditorBackground));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 3.0f);

    // Phase 1: the ruler: a tick per beat, the bar numbers on every fourth.
    const auto ruler = rulerArea();
    g.setFont(juce::Font(juce::FontOptions {}.withHeight(mu_ui::sf(10.0f))));
    const int firstBeat = (int) std::floor(beatAt(0)), lastBeat = std::min((int) std::ceil(timeline.lengthBeats()), (int) std::ceil(beatAt(getWidth())) + 1);
    for (int beat = std::max(0, firstBeat); beat < lastBeat; ++beat)
    {
        const int x = xOf((double) beat);
        const bool bar = beat % 4 == 0;
        g.setColour(LF::colour(LF::labelText).withAlpha(bar ? 0.9f : 0.4f));
        g.fillRect(x, ruler.getBottom() - s(bar ? 8 : 4), 1, s(bar ? 8 : 4));
        if (bar) g.drawText(juce::String(beat / 4 + 1), x + s(3), ruler.getY(), s(24), ruler.getHeight(), juce::Justification::centredLeft);
    }

    // Phase 2: the beat lines through the row (the snap grid's own lines are drawn over the blocks, phase 3b).
    const auto row = rowArea();
    const double L = timeline.lengthBeats();
    g.setColour(LF::colour(LF::stepEditorGridLine));
    for (int beat = std::max(0, firstBeat); beat < lastBeat; ++beat)
        g.fillRect(xOf((double) beat), row.getY(), 1, row.getHeight());

    // Phase 3: the blocks.
    const auto accent = LF::appAccent(*this);
    const auto& lighting = LF::lighting();
    for (int i = 0; i < timeline.size(); ++i)
    {
        const auto& b = timeline.blocks()[(size_t) i];
        const int x0 = xOf(b.start), x1 = xOf(b.start + blockBeats(i));
        const auto r = juce::Rectangle<int>(x0, row.getY() + s(LF::kBlockInset), std::max(2, x1 - x0 - 1), row.getHeight() - s(2 * LF::kBlockInset));
        // A hit whose slice was cropped off has no sound: it stays on the timeline, dimmed.
        const bool cropped = b.slice < 0;
        const float dim = cropped ? 0.12f : 1.0f;
        // The part above the hit's level is the dim one; the coloured part below it is drawn over at full strength.
        const float unlit = cropped || b.own.volume >= 0.995f ? 1.0f : 0.3f;
        g.setColour(accent.withAlpha((i == selected ? lighting.blockFillSelected : lighting.blockFill) * dim * unlit));
        g.fillRoundedRectangle(r.toFloat(), LF::kBlockRadius);
        // The hit's volume: the block is coloured in from the foot up to its level (0..100 %), and a line marks the top of it.
        const float vol = b.own.volume;
        if (! cropped && vol > 0.0f)
        {
            const auto lit = r.withTop(r.getBottom() - std::max(1, (int) std::lround(vol * (float) r.getHeight())));
            g.saveState();
            g.reduceClipRegion(lit);
            g.setColour(accent.withAlpha(lighting.blockFillSelected));
            g.fillRoundedRectangle(r.toFloat(), LF::kBlockRadius);
            g.restoreState();
            if (vol < 0.995f)
            {
                g.setColour(accent.brighter(0.5f));
                g.fillRect(lit.getX() + 1, lit.getY(), lit.getWidth() - 2, 2);
            }
        }
        g.setColour(LF::colour(LF::lfoEditorPoint).withAlpha((i == selected ? 1.0f : 0.5f) * dim));
        g.drawRoundedRectangle(r.toFloat(), LF::kBlockRadius, 1.0f);
        if (! loopPeaks.empty() && b.slice >= 0 && b.slice < (int) sliceSpans.size())
        {
            // The part of the loop the hit plays: from the trim, for the block's width at the hit's speed; past the slice's end it is the
            // audio that follows, and the picture stops where the audio does rather than stretching.
            const auto& span = sliceSpans[(size_t) b.slice];
            const double nat = std::max(1.0e-6, naturalBeatsOf(i));
            const double u0 = b.own.trimInBeats, u1 = u0 + blockBeats(i) / std::max(0.25f, b.own.retime);
            const double a = span.start + u0 / nat * span.length, e = span.start + u1 / nat * span.length;
            const double avail = std::min(e, 1.0);
            auto rr = r.reduced(1);
            rr = rr.withSizeKeepingCentre(rr.getWidth(), std::max(2, (int) std::lround((float) rr.getHeight() * vol)));   // the picture follows the level
            if (e > a) rr.setWidth(std::max(0, (int) std::lround((double) rr.getWidth() * (avail - a) / (e - a))));
            g.setColour(juce::Colours::black.withAlpha(lighting.blockWave));
            drawPeaks(g, loopPeaks, rr, b.own.reverse, 0, getWidth(), a, avail);
        }
        else if (b.slice >= 0 && b.slice < (int) slicePeaksList.size())
        {
            g.setColour(juce::Colours::black.withAlpha(lighting.blockWave));
            // Only the part of the slice that sounds is drawn: it starts after the trim and ends where the block does, at the hit's speed.
            const double nat = std::max(1.0e-6, naturalBeatsOf(i)), from = std::clamp(b.own.trimInBeats / nat, 0.0, 1.0);
            const double to = std::clamp(from + blockBeats(i) / std::max(0.25f, b.own.retime) / nat, from, 1.0);
            const auto pr = r.reduced(1);
            drawPeaks(g, slicePeaksList[(size_t) b.slice], pr.withSizeKeepingCentre(pr.getWidth(), std::max(2, (int) std::lround((float) pr.getHeight() * vol))), b.own.reverse, 0, getWidth(), from, to);
        }
        if (r.getWidth() > s(LF::kBlockLabelMinW))
        {
            g.setColour(juce::Colours::black.withAlpha(lighting.blockLabel * dim));
            g.drawText((cropped ? juce::String("cropped") : juce::String(b.slice + 1)) + (b.own.reverse ? " <" : ""), r.reduced(s(3), 0), juce::Justification::centredLeft);
        }
    }

    // Phase 3b: the snap grid: a line for every step (sixteenths, triplets...) when the steps are far enough apart to read, over the blocks
    // too, and a tick in the ruler. Triplet steps are drawn in the accent so they are not mistaken for straight ones.
    const double pxPerBeat = (double) getWidth() / std::max(1.0e-6, viewBeats > 0.0 ? viewBeats : L);
    if (gridBeats > 0.0 && gridBeats < 1.0 - 1.0e-9 && gridBeats * pxPerBeat >= (double) s(4))
    {
        const bool triplet = std::abs(std::log2(gridBeats) - std::round(std::log2(gridBeats))) > 1.0e-6;
        const auto line = triplet ? LF::appAccent(*this).brighter(0.4f) : juce::Colours::white;
        const long first = (long) std::floor(beatAt(0) / gridBeats), last = (long) std::ceil(std::min(L, beatAt(getWidth())) / gridBeats);
        for (long i = std::max(0L, first); i <= last; ++i)
        {
            const int x = xOf((double) i * gridBeats);
            g.setColour(line.withAlpha(lighting.snapLine));
            g.fillRect(x, row.getY(), 1, row.getHeight());
            g.setColour(line.withAlpha(lighting.snapTick));
            g.fillRect(x, ruler.getBottom() - s(3), 1, s(3));
        }
    }

    // The insertion line of a drop in progress, with the dragged slice's waveform where it would land.
    if (dropBeat >= 0.0)
    {
        if (dropSlice >= 0 && dropSlice < (int) sliceBeatsList.size())
        {
            const int gx0 = xOf(dropBeat), gx1 = xOf(dropBeat + sliceBeatsList[(size_t) dropSlice]);
            const auto gr = juce::Rectangle<int>(gx0, row.getY() + s(LF::kBlockInset), std::max(2, gx1 - gx0 - 1), row.getHeight() - s(2 * LF::kBlockInset));
            g.setColour(accent.withAlpha(lighting.ghostFill));
            g.fillRoundedRectangle(gr.toFloat(), LF::kBlockRadius);
            g.setColour(accent);
            g.drawRoundedRectangle(gr.toFloat(), LF::kBlockRadius, 1.0f);
            if (dropSlice < (int) slicePeaksList.size())
            {
                g.setColour(juce::Colours::black.withAlpha(lighting.ghostWave));
                drawPeaks(g, slicePeaksList[(size_t) dropSlice], gr.reduced(1), false, 0, getWidth());
            }
        }
        g.setColour(accent);
        g.fillRect(xOf(dropBeat) - 1, row.getY(), 3, row.getHeight());
    }

    // Phase 4: the playhead.
    if (playhead >= 0.0)
    {
        g.setColour(LF::colour(LF::lfoEditorPlayhead));
        g.fillRect(xOf(std::fmod(playhead, timeline.lengthBeats())), 0, 2, getHeight());
    }

    // A zoomed view shows which part of the timeline it is: a thin bar under the ruler.
    if (viewBeats > 0.0)
    {
        g.setColour(LF::colour(LF::labelText).withAlpha(0.35f));
        g.fillRect((int) std::lround(viewStart / L * getWidth()), ruler.getBottom() - 2, std::max(4, (int) std::lround(viewBeats / L * getWidth())), 2);
    }
}

void PhraseTimeline::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    if (e.mods.isMiddleButtonDown() || rulerArea().contains(e.getPosition()))   // a middle-button or ruler drag pans the view
    {
        panning = true;
        panStartBeat = viewStart;
        return;
    }
    if (! rowArea().contains(e.getPosition())) return;
    dragMoved = false;
    bool onRight = false;
    const int edgeBlock = edgeAt(e.x, onRight);
    const int levelBlock = edgeBlock < 0 ? levelHandleAt(e.getPosition()) : -1;
    const int hit = edgeBlock >= 0 ? edgeBlock : levelBlock >= 0 ? levelBlock : blockAt(e.x);
    if (hit >= 0)
    {
        selected = hit;
        dragBlock = hit;
        dragLevel = levelBlock >= 0;
        dragEdge = edgeBlock >= 0 ? (onRight ? Edge::Right : Edge::Left) : Edge::None;
        const auto& hb = timeline.blocks()[(size_t) hit];
        dragAnchor = dragEdge == Edge::Left ? hb.start + blockBeats(hit) : hb.start;   // the edge that holds still
        grabOffset = beatAt(e.x) - timeline.blocks()[(size_t) hit].start;
        dragOrigin = timeline;
        dragOriginIndex = hit;
        if (onBlockSelected) onBlockSelected(hit);
        repaint();
        return;
    }

    // Empty row: place the selected slice here.
    selected = -1;
    dragBlock = -1;
    const int slice = sliceToPlace ? sliceToPlace() : -1;
    if (slice >= 0 && onEdit)
    {
        const double beat = e.mods.isShiftDown() ? beatAt(e.x) : Timeline::snap(beatAt(e.x), gridBeats);
        const auto start = settingsOfSlice ? settingsOfSlice(slice) : SliceSettings();
        onEdit([slice, beat, start](Timeline& t) { t.place(slice, beat, 0.0, start); });
        selected = timeline.indexAt(beat);   // the edit has been published: the new hit is the one at this beat
        if (selected >= 0 && std::abs(timeline.blocks()[(size_t) selected].start - beat) > 1.0e-6) selected = -1;
        if (onBlockSelected) onBlockSelected(selected);
        repaint();
        return;
    }
    if (onBlockSelected) onBlockSelected(-1);
    repaint();
}

void PhraseTimeline::mouseDrag(const juce::MouseEvent& e)
{
    if (panning)
    {
        const double L = timeline.lengthBeats();
        const double shown = viewBeats > 0.0 ? viewBeats : L;
        if (viewBeats <= 0.0) viewBeats = shown;   // clampView turns a full-length view back into "all of it"
        viewStart = panStartBeat - (double) e.getDistanceFromDragStartX() / (double) std::max(1, getWidth()) * shown;
        clampView();
        repaint();
        return;
    }
    if (dragBlock < 0 || ! onEdit) return;

    // Dragging the top of a block up or down sets the hit's volume: 0 at the foot, 100 % at the top, no snapping.
    if (dragLevel)
    {
        const int at = dragBlock;
        const auto r = blockRect(at);
        const float v = juce::jlimit(0.0f, 1.0f, (float) (r.getBottom() - e.y) / (float) std::max(1, r.getHeight()));
        onEdit([at, v](Timeline& t)
        {
            auto own = t.blocks()[(size_t) at].own;
            own.volume = v;
            t.setSettings(at, own);
        });
        return;
    }

    // Dragging an edge trims the block (the start edge skips the start of the slice, the end edge stops it early); with Alt the
    // slice is retimed to the new width instead. The edge snaps to the grid unless Shift is held; at least a sixteenth of a beat stays.
    if (dragEdge != Edge::None)
    {
        const int at = dragBlock;
        const double grid = e.mods.isShiftDown() ? 0.0 : gridBeats;
        const double beat = Timeline::snap(beatAt(e.x), grid);
        const double nat = naturalBeatsOf(at);
        constexpr double kMin = 0.0625;
        const bool left = dragEdge == Edge::Left;
        int moved = -1;
        if (e.mods.isAltDown())
        {
            const double a = left ? std::min(beat, dragAnchor - kMin) : dragAnchor, b = left ? dragAnchor : std::max(beat, dragAnchor + kMin);
            onEdit([&](Timeline& t) { moved = t.retime(at, a, b, nat); });
        }
        else if (left)
            onEdit([&](Timeline& t) { moved = t.trimStart(at, std::min(beat, dragAnchor - kMin), nat); });
        else
        {
            const double len = std::max(kMin, beat - dragAnchor);
            if (std::abs(len - blockBeats(at)) > 1.0e-9) onEdit([&](Timeline& t) { moved = t.setBlockLength(at, len, 0.0) ? at : -1; });
        }
        if (moved >= 0) { dragBlock = moved; selected = moved; }
        return;
    }

    const double grid = e.mods.isShiftDown() ? 0.0 : gridBeats;
    const double target = Timeline::snap(beatAt(e.x) - grabOffset, grid);
    // The block count can change under a drag (a drop splits what it covers), so the start comes from the drag's own record.
    if (dragOriginIndex < 0 || dragOriginIndex >= dragOrigin.size()) return;
    const double current = dragMoved ? lastDropStart : dragOrigin.blocks()[(size_t) dragOriginIndex].start;

    // Ctrl-drag copies (the original stays where it was). Every step starts again from the timeline as the drag began, so a block
    // dragged across others only overlays them for now; what it covers comes back when it moves on.
    const bool copy = e.mods.isCtrlDown();
    if (std::abs(target - current) < 1.0e-9 && (dragMoved || ! copy)) return;

    int movedTo = -1;
    const Timeline origin = dragOrigin;
    const int from = dragOriginIndex;
    const auto beats = sliceBeatsList;
    onEdit([&](Timeline& t) { t = origin; movedTo = t.drop(from, target, copy, beats); });
    if (movedTo >= 0)
    {
        dragBlock = movedTo; selected = movedTo;   // the timeline republishes; the block we hold is at its new index
        dragMoved = true;
        lastDropStart = target;
    }
}

void PhraseTimeline::mouseUp(const juce::MouseEvent&)
{
    panning = false;
    dragBlock = -1;
    dragLevel = false;
    dragEdge = Edge::None;
}

void PhraseTimeline::mouseMove(const juce::MouseEvent& e)
{
    bool right = false;
    const int hit = rowArea().contains(e.getPosition()) ? edgeAt(e.x, right) : -1;
    const bool onLevel = hit < 0 && levelHandleAt(e.getPosition()) >= 0;
    setMouseCursor(hit >= 0 ? juce::MouseCursor::LeftRightResizeCursor : onLevel ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
}

void PhraseTimeline::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const double L = timeline.lengthBeats();
    if (L <= 0.0) return;
    const bool horizontal = e.mods.isShiftDown() || std::abs(w.deltaX) > std::abs(w.deltaY);
    if (horizontal)
    {
        // Scroll by a fraction of the view (Shift+wheel or a trackpad's horizontal delta).
        const double shown = viewBeats > 0.0 ? viewBeats : L;
        const double delta = std::abs(w.deltaX) > std::abs(w.deltaY) ? w.deltaX : w.deltaY;
        viewBeats = shown;
        viewStart -= (double) delta * shown * 0.5;
        clampView();
        repaint();
    }
    else
        zoomBy(std::pow(1.25, (double) w.deltaY * 5.0), e.x);   // the wheel zooms around the pointer
}

bool PhraseTimeline::keyPressed(const juce::KeyPress& k)
{
    if ((k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) && selected >= 0 && onEdit)
    {
        const int at = selected;
        selected = -1;
        onEdit([at](Timeline& t) { t.remove(at); });
        if (onBlockSelected) onBlockSelected(-1);
        return true;
    }
    return false;
}

} // namespace mu_tilate
