#include "SegmentControl.h"

SegmentControl::SegmentControl(std::initializer_list<juce::String> labels,
                               ActiveStyle s, DrawStyle d)
    : options(labels), style(s), drawStyle(d)
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
    using Id = MuLookAndFeel::ColourIds;
    switch (style)
    {
        case ActiveStyle::Positive:
            return { MuLookAndFeel::colour(Id::segmentPositiveBg),
                     MuLookAndFeel::colour(Id::segmentPositiveBorder) };
        case ActiveStyle::Warning:
            return { MuLookAndFeel::colour(Id::segmentWarningBg),
                     MuLookAndFeel::colour(Id::segmentWarningBorder) };
        default:
            return { MuLookAndFeel::colour(Id::segmentActiveBg),
                     MuLookAndFeel::colour(Id::segmentActiveBorder) };
    }
}

void SegmentControl::paint(juce::Graphics& g)
{
    using Id = MuLookAndFeel::ColourIds;
    const int n = (int)options.size();
    if (n == 0) return;

    auto [activeBg, activeBorder] = activeColours();

    if (drawStyle == DrawStyle::Pills)
    {
        // Each segment is an individual rounded button. Gap and the font
        // clamp range scale with mu_ui::scale so the pills retain their
        // proportions at Phase 2 Large / Small variants — at scale = 1.0
        // these match the historical 2 px / 8..11 pt values.
        using mu_ui::sf;
        const float gap      = sf(2.0f);
        const float pillW    = ((float)getWidth() - gap * (float)(n + 1)) / (float)n;
        const float pillH    = (float)getHeight() - gap * 2.0f;
        const float radius   = pillH * 0.45f;
        const float fontSize = juce::jmax(sf(8.0f), juce::jmin(sf(11.0f), pillH * 0.6f));

        for (int i = 0; i < n; ++i)
        {
            juce::Rectangle<float> pill(gap + (float)i * (pillW + gap), gap, pillW, pillH);
            bool active = (i == selectedIndex);

            g.setColour(active ? activeBg : MuLookAndFeel::colour(Id::segmentInactiveBg));
            g.fillRoundedRectangle(pill, radius);

            g.setColour(active ? activeBorder : MuLookAndFeel::colour(Id::segmentInactiveBorder));
            g.drawRoundedRectangle(pill.reduced(0.5f), radius, 1.0f);

            g.setColour(active ? activeBorder : MuLookAndFeel::colour(Id::segmentInactiveText));
            g.setFont(juce::Font(juce::FontOptions{}.withHeight(fontSize)));
            g.drawText(options[i], pill.toNearestInt(), juce::Justification::centred, true);
        }
    }
    else
    {
        // Bar style: single connected bar with dividers
        using mu_ui::sf;
        const float segW = (float)getWidth() / (float)n;
        const float h    = (float)getHeight();
        const float r    = sf(3.0f);

        for (int i = 0; i < n; ++i)
        {
            juce::Rectangle<float> seg(i * segW, 0.0f, segW, h);
            bool active = (i == selectedIndex);

            g.setColour(active ? activeBg : MuLookAndFeel::colour(Id::segmentInactiveBg));
            if (i == 0)
            {
                g.fillRoundedRectangle(seg, r);
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

            g.setColour(active ? activeBorder : MuLookAndFeel::colour(Id::segmentInactiveBorder));
            if (i > 0) g.drawVerticalLine((int)(i * segW), 0, h);

            g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(11.0f))));
            g.setColour(active ? activeBorder : MuLookAndFeel::colour(Id::segmentInactiveText));
            g.drawText(options[i], seg.toNearestInt(), juce::Justification::centred, true);
        }

        g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBorder));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), r, 1.0f);
    }
}

void SegmentControl::mouseDown(const juce::MouseEvent& e)
{
    const int n = (int)options.size();
    if (n == 0) return;
    const int idx = juce::jlimit(0, n - 1, (int)(e.x / ((float)getWidth() / n)));
    setSelectedIndex(idx, true);
}
