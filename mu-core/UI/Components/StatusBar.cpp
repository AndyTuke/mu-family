#include "StatusBar.h"

StatusBar::StatusBar()
{
}

void StatusBar::showParam(const juce::String& paramName,
                           const juce::String& value,
                           juce::Colour rhythmColour)
{
    currentName  = paramName;
    currentValue = value;
    tagColour    = rhythmColour;
    repaint();
}

void StatusBar::setSupportLinkVisible(bool visible)
{
    if (supportLinkVisible == visible) return;
    supportLinkVisible = visible;
    supportLinkHover   = false;
    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaint();
}

// The link sits at the bar's right edge, vertically centred; sized from the label text.
juce::Rectangle<int> StatusBar::supportLinkBounds() const
{
    using mu_ui::s;
    using mu_ui::sf;
    const juce::Font font(juce::FontOptions{}.withHeight(sf(11.0f)));
    juce::GlyphArrangement ga;
    ga.addLineOfText(font, "Support", 0.0f, 0.0f);
    const int textW = (int) std::ceil(ga.getBoundingBox(0, ga.getNumGlyphs(), true).getWidth());
    const int w = textW + s(16);   // padding either side keeps the click target forgiving
    return { getWidth() - w - s(4), 0, w, getHeight() };
}

void StatusBar::mouseMove(const juce::MouseEvent& e)
{
    const bool over = supportLinkVisible && supportLinkBounds().contains(e.getPosition());
    if (over != supportLinkHover)
    {
        supportLinkHover = over;
        setMouseCursor(over ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void StatusBar::mouseExit(const juce::MouseEvent&)
{
    if (! supportLinkHover) return;
    supportLinkHover = false;
    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaint();
}

void StatusBar::mouseDown(const juce::MouseEvent& e)
{
    if (supportLinkVisible && supportLinkBounds().contains(e.getPosition()) && onSupportClicked)
        onSupportClicked();
}

void StatusBar::paint(juce::Graphics& g)
{
    using Id = MuLookAndFeel::ColourIds;
    using mu_ui::s;
    using mu_ui::sf;

    g.setColour(MuLookAndFeel::colour(Id::statusBarBackground));
    g.fillAll();

    // Support link at the right edge (unlicensed only); drawn first so the parameter text,
    // which is centred, never needs to know about it.
    if (supportLinkVisible)
    {
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(11.0f))));
        g.setColour(MuLookAndFeel::colour(supportLinkHover ? Id::statusBarValue : Id::statusBarText));
        g.drawText("Support", supportLinkBounds(), juce::Justification::centred, false);
    }

    const int h = getHeight();
    int textX = s(8);
    const int tagW = s(kTagWidth);

    // Rhythm colour tag
    if (tagColour.getAlpha() > 0)
    {
        g.setColour(tagColour);
        g.fillRect(0, 0, tagW, h);
        textX += tagW + s(4);
    }

    if (currentName.isEmpty() && currentValue.isEmpty()) return;

    const juce::Font font(juce::FontOptions{}.withHeight(sf(11.0f)));
    g.setFont(font);

    // Measure and centre the name + value pair as a unit inside the bar.
    // Both draw in a full-height strip so centredLeft vertically centres them.
    auto measureW = [&font](const juce::String& t) -> float
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText(font, t, 0.0f, 0.0f);
        return ga.getBoundingBox(0, ga.getNumGlyphs(), true).getWidth();
    };
    const float nameW = measureW(currentName);
    const float sepW  = measureW("  ");
    const float valW  = measureW(currentValue);
    const int   startX = juce::jmax(textX, (int)(((float)getWidth() - nameW - sepW - valW) * 0.5f));

    g.setColour(MuLookAndFeel::colour(Id::statusBarText));
    g.drawText(currentName,  startX,                                   0,
               (int)std::ceil(nameW),     h, juce::Justification::centredLeft, false);
    g.setColour(MuLookAndFeel::colour(Id::statusBarValue));
    g.drawText(currentValue, startX + (int)std::ceil(nameW + sepW),    0,
               (int)std::ceil(valW) + 1, h, juce::Justification::centredLeft, false);
}
