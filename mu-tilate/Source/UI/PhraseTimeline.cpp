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
    if (timeline.lengthBeats() != lastLength) { lastLength = timeline.lengthBeats(); viewBeats = 0.0; viewStart = 0.0; }   // a new length: show all of it
    clampView();
    repaint();
}

void PhraseTimeline::setSelectedBlock(int i)   { selected = i; repaint(); }
void PhraseTimeline::setPlayheadBeats(double b)
{
    if (b == playhead) return;
    playhead = b;

    // While playing, page the view on when the playhead leaves it.
    if (playhead >= 0.0 && viewBeats > 0.0 && viewBeats < timeline.lengthBeats() - 1.0e-9)
    {
        const double p = std::fmod(playhead, timeline.lengthBeats());
        if (p < viewStart || p >= viewStart + viewBeats) { viewStart = std::floor(p / viewBeats) * viewBeats; clampView(); }
    }
    repaint();
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

bool PhraseTimeline::onRightEdge(int i, int x) const
{
    const int edge = xOf(timeline.blocks()[(size_t) i].start + blockBeats(i));
    return std::abs(x - edge) <= mu_ui::s(5);
}

double PhraseTimeline::blockBeats(int i) const
{
    const auto& b = timeline.blocks()[(size_t) i];
    double len = b.length > 0.0 ? b.length
               : (b.slice >= 0 && b.slice < (int) sliceBeatsList.size() ? sliceBeatsList[(size_t) b.slice] : 1.0);
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

    // Phase 2: grid lines through the row.
    const auto row = rowArea();
    g.setColour(LF::colour(LF::stepEditorGridLine));
    for (int beat = std::max(0, firstBeat); beat < lastBeat; ++beat)
        g.fillRect(xOf((double) beat), row.getY(), 1, row.getHeight());

    // Phase 3: the blocks.
    const auto accent = LF::appAccent(*this);
    for (int i = 0; i < timeline.size(); ++i)
    {
        const auto& b = timeline.blocks()[(size_t) i];
        const int x0 = xOf(b.start), x1 = xOf(b.start + blockBeats(i));
        const auto r = juce::Rectangle<int>(x0, row.getY() + s(3), std::max(2, x1 - x0 - 1), row.getHeight() - s(6));
        g.setColour(accent.withAlpha(i == selected ? 0.9f : 0.55f));
        g.fillRoundedRectangle(r.toFloat(), 3.0f);
        g.setColour(LF::colour(LF::lfoEditorPoint).withAlpha(i == selected ? 1.0f : 0.5f));
        g.drawRoundedRectangle(r.toFloat(), 3.0f, 1.0f);
        if (r.getWidth() > s(14))
        {
            g.setColour(juce::Colours::black.withAlpha(0.85f));
            g.drawText(juce::String(b.slice + 1) + (b.reverse ? " <" : ""), r.reduced(s(3), 0), juce::Justification::centredLeft);
        }
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
        const double L = timeline.lengthBeats();
        g.setColour(LF::colour(LF::labelText).withAlpha(0.35f));
        g.fillRect((int) std::lround(viewStart / L * getWidth()), ruler.getBottom() - 2, std::max(4, (int) std::lround(viewBeats / L * getWidth())), 2);
    }
}

void PhraseTimeline::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    if (! rowArea().contains(e.getPosition())) return;
    const int hit = blockAt(e.x);
    dragMoved = false;
    if (hit >= 0)
    {
        selected = hit;
        dragBlock = hit;
        resizing = onRightEdge(hit, e.x);
        grabOffset = beatAt(e.x) - timeline.blocks()[(size_t) hit].start;
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
        const double beat = e.mods.isAltDown() ? beatAt(e.x) : Timeline::snap(beatAt(e.x), gridBeats);
        onEdit([slice, beat](Timeline& t) { t.place(slice, beat, 0.0); });
    }
    if (onBlockSelected) onBlockSelected(-1);
    repaint();
}

void PhraseTimeline::mouseDrag(const juce::MouseEvent& e)
{
    if (dragBlock < 0 || ! onEdit) return;

    // Dragging the right edge sets the block's length (at least a sixteenth of a beat), snapped to the grid unless Alt is held.
    if (resizing)
    {
        const int at = dragBlock;
        const double grid = e.mods.isAltDown() ? 0.0 : gridBeats;
        const double len = std::max(0.0625, Timeline::snap(beatAt(e.x) - timeline.blocks()[(size_t) at].start, grid));
        if (std::abs(len - blockBeats(at)) > 1.0e-9) onEdit([at, len](Timeline& t) { t.setBlockLength(at, len, 0.0); });
        return;
    }

    const double grid = e.mods.isAltDown() ? 0.0 : gridBeats;
    const double target = Timeline::snap(beatAt(e.x) - grabOffset, grid);
    const double current = timeline.blocks()[(size_t) dragBlock].start;
    if (std::abs(target - current) < 1.0e-9 && ! (e.mods.isCtrlDown() && ! dragMoved)) return;

    const int from = dragBlock;
    const bool copy = e.mods.isCtrlDown();
    int movedTo = -1;
    onEdit([&](Timeline& t) { movedTo = copy ? t.copy(from, target, 0.0) : t.move(from, target, 0.0); });
    if (movedTo >= 0 && ! copy)
    {
        dragBlock = movedTo; selected = movedTo;   // the timeline republishes; the block we hold is at its new index
        dragMoved = true;
    }
    else if (copy)
        dragMoved = true;
}

void PhraseTimeline::mouseUp(const juce::MouseEvent&)
{
    dragBlock = -1;
    resizing = false;
}

void PhraseTimeline::mouseMove(const juce::MouseEvent& e)
{
    const int hit = rowArea().contains(e.getPosition()) ? blockAt(e.x) : -1;
    setMouseCursor(hit >= 0 && onRightEdge(hit, e.x) ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void PhraseTimeline::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const double L = timeline.lengthBeats();
    if (L <= 0.0) return;
    const double shown = viewBeats > 0.0 ? viewBeats : L;
    if (e.mods.isCtrlDown())
    {
        // Zoom around the beat under the pointer.
        const double anchor = beatAt(e.x);
        const double frac = (double) e.x / (double) std::max(1, getWidth());
        viewBeats = shown * std::pow(0.8, (double) w.deltaY * 5.0);
        viewStart = anchor - frac * viewBeats;
    }
    else
    {
        // Scroll by a tenth of the view (a trackpad's horizontal delta works too).
        const double delta = std::abs(w.deltaX) > std::abs(w.deltaY) ? w.deltaX : w.deltaY;
        viewBeats = shown;
        viewStart -= (double) delta * 2.0 * shown * 0.1 * 5.0;
    }
    clampView();
    repaint();
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
