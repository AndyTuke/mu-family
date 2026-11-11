#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/Components/MuLookAndFeel.h"
#include "UI/StatusSink.h"

#include <algorithm>
#include <functional>

namespace mu_on
{

// A step's loop as one control, in the Bitwig style: the Length as a number on the left (drag it up or down to change it, 1..8) and, filling
// the rest of the window, one segment per bar of that Length. A lit segment is a bar the step plays in; clicking a segment switches it. The
// window keeps its size, so the segments get narrower as the Length grows. The owner holds the data: it sets the Length and mask, and gets
// onToggle / onLength.
class StepBarsSelector : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr int kMaxBars = 8;
    std::function<void(int bar, bool on)> onToggle;   // bar is 1-based
    std::function<void(int lengthBars)>   onLength;

    StepBarsSelector() { setMouseCursor(juce::MouseCursor::PointingHandCursor); setTooltip("Loops the step spans (drag the number) and the loops it plays in (click one). A loop is one pass through the whole step row"); }

    void set(int lengthBars, int mask)
    {
        lengthBars = juce::jlimit(1, kMaxBars, lengthBars);
        if (lengthBars == length && mask == bits) return;
        length = lengthBars;
        bits = mask;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        const auto lit = MuLookAndFeel::lcdLitColour();
        MuLookAndFeel::drawLcdGlass(g, b, lit, false);
        g.setColour(lit);
        g.setFont(MuLookAndFeel::lcdFont(juce::jmin(15.0f, b.getHeight() * 0.6f)));
        g.drawText(juce::String(length), numberArea().toFloat(), juce::Justification::centred);
        const auto bars = barsArea();
        const float w = bars.getWidth() / (float) length;
        for (int i = 0; i < length; ++i)
        {
            const bool on = (bits >> i & 1) != 0;
            const auto cell = juce::Rectangle<float>(bars.getX() + w * (float) i, bars.getY(), w, bars.getHeight()).reduced(1.5f);
            g.setColour(on ? lit.withAlpha(0.85f) : lit.withAlpha(0.15f));
            g.fillRoundedRectangle(cell, 1.5f);
        }
        MuLookAndFeel::drawLcdFront(g, b);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (numberArea().contains(e.getPosition()))
        {
            dragging = true;
            dragStartY = e.getPosition().y;
            dragStartLength = length;
            return;
        }
        const auto bars = barsArea();
        const int bar = juce::jlimit(0, length - 1, (int) ((e.position.x - bars.getX()) / (bars.getWidth() / (float) length)));
        if (length <= 1)   // a one-loop step plays every pass: there is nothing to switch off
        {
            mu_ui::reportStatus(*this, "Step Loops", describe());
            return;
        }
        const bool on = (bits >> bar & 1) == 0;
        bits = on ? (bits | (1 << bar)) : (bits & ~(1 << bar));
        repaint();
        if (onToggle) onToggle(bar + 1, on);
        mu_ui::reportStatus(*this, "Step Loops", describe());
    }

    // Dragging the number up lengthens the loop, down shortens it, one bar per few pixels.
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (! dragging) return;
        const int next = juce::jlimit(1, kMaxBars, dragStartLength + (dragStartY - e.getPosition().y) / 6);
        if (next == length) return;
        length = next;
        repaint();
        if (onLength) onLength(length);
        mu_ui::reportStatus(*this, "Step Loops", juce::String(length));
    }
    void mouseUp(const juce::MouseEvent&) override { dragging = false; }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (! numberArea().contains(e.getPosition()) || w.deltaY == 0.0f) return;
        const int next = juce::jlimit(1, kMaxBars, length + (w.deltaY > 0.0f ? 1 : -1));
        if (next == length) return;
        length = next;
        repaint();
        if (onLength) onLength(length);
        mu_ui::reportStatus(*this, "Step Loops", juce::String(length));
    }
    void mouseEnter(const juce::MouseEvent&) override { mu_ui::reportStatus(*this, "Step Loops", describe() + " of " + juce::String(length)); }

private:
    juce::Rectangle<int> numberArea() const { return getLocalBounds().removeFromLeft(juce::jmax(18, getHeight())); }
    juce::Rectangle<float> barsArea() const { return getLocalBounds().withTrimmedLeft(numberArea().getWidth()).toFloat().reduced(3.0f); }

    // The ticked bars within the Length as text for the status bar: "1, 3, 4" or "None".
    juce::String describe() const
    {
        juce::StringArray on;
        for (int i = 0; i < length; ++i) if (bits >> i & 1) on.add(juce::String(i + 1));
        return on.isEmpty() ? "None" : on.joinIntoString(", ");
    }

    int length = 1, bits = 0xFF;
    bool dragging = false;
    int dragStartY = 0, dragStartLength = 1;
};

} // namespace mu_on
