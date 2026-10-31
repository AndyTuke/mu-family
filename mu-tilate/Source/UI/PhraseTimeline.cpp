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
    repaint();
}

void PhraseTimeline::setSelectedBlock(int i)   { selected = i; repaint(); }
void PhraseTimeline::setPlayheadBeats(double b) { if (b != playhead) { playhead = b; repaint(); } }

juce::Rectangle<int> PhraseTimeline::rulerArea() const { return getLocalBounds().removeFromTop(mu_ui::s(kRulerH)); }
juce::Rectangle<int> PhraseTimeline::rowArea() const   { auto r = getLocalBounds(); r.removeFromTop(mu_ui::s(kRulerH)); return r; }

int PhraseTimeline::xOf(double beat) const noexcept
{
    return (int) std::lround(beat / std::max(1.0e-6, timeline.lengthBeats()) * (double) getWidth());
}

double PhraseTimeline::beatAt(int x) const noexcept
{
    return (double) x / (double) std::max(1, getWidth()) * timeline.lengthBeats();
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
    for (int beat = 0; beat < (int) std::ceil(timeline.lengthBeats()); ++beat)
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
    for (int beat = 0; beat < (int) std::ceil(timeline.lengthBeats()); ++beat)
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
