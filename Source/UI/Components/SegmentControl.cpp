#include "SegmentControl.h"

SegmentControl::SegmentControl(std::initializer_list<juce::String> labels, ActiveStyle s)
    : options(labels), style(s)
{
}

void SegmentControl::setSelectedIndex(int index, bool notify)
{
    if (index == selectedIndex) return;
    selectedIndex = index;
    repaint();
    if (notify && onChange) onChange(selectedIndex);
}

std::pair<juce::Colour, juce::Colour> SegmentControl::activeColours() const noexcept
{
    using Id = MuClidLookAndFeel::ColourIds;
    switch (style)
    {
        case ActiveStyle::Positive:
            return { MuClidLookAndFeel::colour(Id::segmentPositiveBg),
                     MuClidLookAndFeel::colour(Id::segmentPositiveBorder) };
        case ActiveStyle::Warning:
            return { MuClidLookAndFeel::colour(Id::segmentWarningBg),
                     MuClidLookAndFeel::colour(Id::segmentWarningBorder) };
        default:
            return { MuClidLookAndFeel::colour(Id::segmentActiveBg),
                     MuClidLookAndFeel::colour(Id::segmentActiveBorder) };
    }
}

void SegmentControl::paint(juce::Graphics& g)
{
    using Id = MuClidLookAndFeel::ColourIds;
    const int n = (int)options.size();
    if (n == 0) return;

    const float segW = (float)getWidth() / n;
    const float h    = (float)getHeight();
    const float r    = 3.0f;
    auto [activeBg, activeBorder] = activeColours();

    for (int i = 0; i < n; ++i)
    {
        juce::Rectangle<float> seg(i * segW, 0.0f, segW, h);
        bool active = (i == selectedIndex);

        g.setColour(active ? activeBg : MuClidLookAndFeel::colour(Id::segmentInactiveBg));
        if (i == 0)
        {
            g.fillRoundedRectangle(seg, r);
            // Fill right portion straight for non-last segment
            if (n > 1) g.fillRect(seg.withLeft(seg.getX() + r));
        }
        else if (i == n - 1)
        {
            g.fillRoundedRectangle(seg, r);
            g.fillRect(seg.withRight(seg.getRight() - r));
        }
        else
        {
            g.fillRect(seg);
        }

        g.setColour(active ? activeBorder : MuClidLookAndFeel::colour(Id::segmentInactiveBorder));
        // Draw segment dividers
        if (i > 0) g.drawVerticalLine((int)(i * segW), 0, h);

        g.setFont(juce::Font(11.0f));
        g.setColour(active ? activeBorder : MuClidLookAndFeel::colour(Id::segmentInactiveText));
        g.drawText(options[i], seg.toNearestInt(), juce::Justification::centred, true);
    }

    // Outer border
    g.setColour(MuClidLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), r, 1.0f);
}

void SegmentControl::mouseDown(const juce::MouseEvent& e)
{
    const int n = (int)options.size();
    if (n == 0) return;
    const int idx = juce::jlimit(0, n - 1, (int)(e.x / ((float)getWidth() / n)));
    setSelectedIndex(idx, true);
}
