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

// ── Toolbox button — procedural vector icons ──────────────────────────────
GateToolButton::GateToolButton(GateTool t)
    : juce::Button(juce::String()), toolId(t)
{
    setClickingTogglesState(true);
    setRadioGroupId(0x6a7e);   // arbitrary shared id so the 4 tools are exclusive
}

void GateToolButton::paintButton(juce::Graphics& g, bool highlighted, bool /*down*/)
{
    using Id = MuLookAndFeel::ColourIds;
    const bool on = getToggleState();
    auto r = getLocalBounds().toFloat().reduced(2.0f);

    // Button background — brighter when selected, subtle on hover.
    g.setColour(on ? MuLookAndFeel::colour(Id::segmentActiveBg)
                   : (highlighted ? MuLookAndFeel::colour(Id::segmentInactiveBg).brighter(0.1f)
                                  : MuLookAndFeel::colour(Id::segmentInactiveBg)));
    g.fillRoundedRectangle(r, 3.0f);
    g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawRoundedRectangle(r, 3.0f, 1.0f);

    const auto ink = on ? MuLookAndFeel::colour(Id::textBright)
                        : MuLookAndFeel::colour(Id::labelText);
    g.setColour(ink);
    auto b = r.reduced(r.getWidth() * 0.22f);

    switch (toolId)
    {
        case GateTool::Pencil:
        {
            // Diagonal pencil: shaft line from bottom-left to top-right + a tip.
            juce::Path p;
            p.startNewSubPath(b.getX(), b.getBottom());
            p.lineTo(b.getRight(), b.getY());
            g.strokePath(p, juce::PathStrokeType(1.6f));
            // tip triangle at bottom-left
            juce::Path tip;
            tip.addTriangle(b.getX(), b.getBottom(),
                            b.getX() + b.getWidth() * 0.28f, b.getBottom() - b.getHeight() * 0.12f,
                            b.getX() + b.getWidth() * 0.12f, b.getBottom() - b.getHeight() * 0.28f);
            g.fillPath(tip);
            break;
        }
        case GateTool::Eraser:
        {
            // Rubber block — a parallelogram.
            juce::Path p;
            p.startNewSubPath(b.getX() + b.getWidth() * 0.2f, b.getBottom());
            p.lineTo(b.getRight(),                          b.getBottom());
            p.lineTo(b.getRight() - b.getWidth() * 0.2f,    b.getY());
            p.lineTo(b.getX(),                              b.getY());
            p.closeSubPath();
            g.strokePath(p, juce::PathStrokeType(1.4f));
            break;
        }
        case GateTool::Glue:
        {
            // Glue droplet — a teardrop (circle + point on top).
            const float cx = b.getCentreX();
            const float cy = b.getCentreY() + b.getHeight() * 0.12f;
            const float rad = b.getWidth() * 0.32f;
            g.fillEllipse(cx - rad, cy - rad, rad * 2.0f, rad * 2.0f);
            juce::Path drop;
            drop.addTriangle(cx, b.getY(),
                             cx - rad * 0.7f, cy,
                             cx + rad * 0.7f, cy);
            g.fillPath(drop);
            break;
        }
        case GateTool::Reverse:
        {
            // Two opposing horizontal arrows.
            const float midY = b.getCentreY();
            const float q    = b.getHeight() * 0.22f;
            juce::Path p;
            // top arrow pointing right
            p.startNewSubPath(b.getX(), midY - q);
            p.lineTo(b.getRight(), midY - q);
            // bottom arrow pointing left
            p.startNewSubPath(b.getRight(), midY + q);
            p.lineTo(b.getX(), midY + q);
            g.strokePath(p, juce::PathStrokeType(1.4f));
            // arrowheads
            juce::Path heads;
            heads.addTriangle(b.getRight(), midY - q,
                              b.getRight() - q, midY - q - q * 0.6f,
                              b.getRight() - q, midY - q + q * 0.6f);
            heads.addTriangle(b.getX(), midY + q,
                              b.getX() + q, midY + q - q * 0.6f,
                              b.getX() + q, midY + q + q * 0.6f);
            g.fillPath(heads);
            break;
        }
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

    // ── Toolbox (pencil / eraser / glue / reverse) ──────────────────────────
    for (auto* b : { &pencilBtn, &eraserBtn, &glueBtn, &reverseBtn })
    {
        b->setTooltip(juce::String());   // roles TBD
        b->onClick = [this, b] { selectTool(b->tool()); };
        addAndMakeVisible(b);
    }
    pencilBtn.setToggleState(true, juce::dontSendNotification);
}

void GatingDesigner::selectTool(GateTool t)
{
    currentTool = t;
    // The radio group keeps button toggle-state exclusive; nothing else to do
    // until the tools' edit roles are wired to the (future) drawable canvas.
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

    // Toolbox row — sits after the "Gating" title (which paint() draws at ~x=6
    // spanning ~120 px), left of the Grid label.
    const int toolW = s(kToolW);
    const int toolGap = s(kToolGap);
    const int toolY = (hdrH - toolW) / 2;
    int tx = s(kHeaderInset) + s(70);   // past the "Gating" title text
    for (auto* b : { &pencilBtn, &eraserBtn, &glueBtn, &reverseBtn })
    {
        b->setBounds(tx, toolY, toolW, toolW);
        tx += toolW + toolGap;
    }
}

} // namespace mu_tant
