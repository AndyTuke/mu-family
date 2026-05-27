#include "GatingDesigner.h"

namespace mu_tant
{

namespace
{
    // Subdivision dropdown entries — denominator value packed into dropdown ID.
    struct SubdivEntry { int denom; const char* label; };
    constexpr SubdivEntry kSubdivOptions[] = {
        { 4,  "1/4"  },
        { 8,  "1/8"  },
        { 16, "1/16" },
        { 32, "1/32" },
    };
    constexpr int kSubdivCount = (int) (sizeof(kSubdivOptions) / sizeof(kSubdivOptions[0]));

    int idForDenom(int denom)
    {
        for (int i = 0; i < kSubdivCount; ++i)
            if (kSubdivOptions[i].denom == denom) return i + 1;
        return 3; // fallback 1/16
    }
}

GatingDesigner::GatingDesigner()
{
    subdivLabel.setText("Grid", juce::dontSendNotification);
    subdivLabel.setJustificationType(juce::Justification::centredRight);
    subdivLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    addAndMakeVisible(subdivLabel);

    for (int i = 0; i < kSubdivCount; ++i)
        subdivDropdown.addItem(kSubdivOptions[i].label, i + 1);
    subdivDropdown.setSelectedId(idForDenom(subdivisionDenom), false);
    subdivDropdown.onChange = [this](int id)
    {
        if (id < 1 || id > kSubdivCount) return;
        setSubdivision(kSubdivOptions[id - 1].denom);
    };
    addAndMakeVisible(subdivDropdown);
}

void GatingDesigner::setSubdivision(int denominator)
{
    if (denominator == subdivisionDenom && boundPattern == nullptr) return;
    subdivisionDenom = denominator;
    subdivDropdown.setSelectedId(idForDenom(denominator), false);
    if (boundPattern != nullptr)
        boundPattern->subdivision = static_cast<GatePattern::Subdivision>(denominator);
    repaint();
}

void GatingDesigner::setPattern(GatePattern* pattern)
{
    boundPattern = pattern;
    if (pattern != nullptr)
    {
        // Pull the bound pattern's subdivision so the UI shows the persisted
        // value when the user switches voices.
        subdivisionDenom = static_cast<int>(pattern->subdivision);
        subdivDropdown.setSelectedId(idForDenom(subdivisionDenom), false);
        repaint();
    }
}

int GatingDesigner::cellCount() const noexcept
{
    // 1 bar = 4 quarter notes. Cells per bar = 4 * (denom/4) = denom.
    // Over 2 bars: cellCount = 2 * denom.
    return kTotalBars * subdivisionDenom;
}

void GatingDesigner::paint(juce::Graphics& g)
{
    using mu_ui::s;
    using mu_ui::sf;
    using Id = MuLookAndFeel::ColourIds;

    // Header strip with title text on the left.
    g.setColour(MuLookAndFeel::colour(Id::panelBackground));
    g.fillRect(0, 0, getWidth(), s(kHeaderH));
    g.setColour(MuLookAndFeel::colour(Id::headingText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(12.0f))));
    g.drawText("Gating", s(kHeaderInset), 0, s(120), s(kHeaderH),
               juce::Justification::centredLeft, false);

    // Gate rectangle — full width below the header.
    const juce::Rectangle<float> gateRect(0.0f,
                                          (float) s(kHeaderH),
                                          (float) getWidth(),
                                          (float) s(kGridH));
    g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBg));
    g.fillRect(gateRect);
    g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawRect(gateRect, 1.0f);

    // Subdivision gridlines. Two bars total — bar boundary is the bolder line.
    const int cells = cellCount();
    if (cells <= 0) return;
    const float cellW = gateRect.getWidth() / (float) cells;
    const int cellsPerBar = cells / kTotalBars;

    for (int i = 1; i < cells; ++i)
    {
        const float x = gateRect.getX() + cellW * (float) i;
        const bool isBarLine = (i % cellsPerBar) == 0;
        g.setColour(isBarLine
                    ? MuLookAndFeel::colour(Id::headingText).withAlpha(0.55f)
                    : MuLookAndFeel::colour(Id::mutedText).withAlpha(0.25f));
        g.fillRect(x, gateRect.getY() + sf(2.0f), isBarLine ? sf(1.5f) : sf(1.0f),
                   gateRect.getHeight() - sf(4.0f));
    }

    // "1 / 2" bar markers along the bottom edge for orientation.
    g.setColour(MuLookAndFeel::colour(Id::mutedText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(9.0f))));
    for (int bar = 0; bar < kTotalBars; ++bar)
    {
        const float x = gateRect.getX() + (gateRect.getWidth() / kTotalBars) * (float) bar;
        g.drawText(juce::String(bar + 1),
                   juce::Rectangle<float>(x + sf(4.0f),
                                          gateRect.getBottom() - sf(14.0f),
                                          sf(14.0f), sf(12.0f)),
                   juce::Justification::centredLeft, false);
    }
}

void GatingDesigner::resized()
{
    using mu_ui::s;
    const int w = getWidth();
    const int hdrH = s(kHeaderH);
    const int ddW  = s(kDropdownW);
    const int ddH  = hdrH - s(2);
    // Header right side: [Grid] [dropdown]
    subdivDropdown.setBounds(w - ddW - s(kHeaderInset), s(1), ddW, ddH);
    subdivLabel   .setBounds(w - ddW - s(kHeaderInset) - s(40), s(1), s(36), ddH);
}

} // namespace mu_tant
