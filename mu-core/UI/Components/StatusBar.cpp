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

// A link is vertically centred and sized from its text; its right edge is `rightEdge`.
juce::Rectangle<int> StatusBar::linkBoundsFor(const juce::String& text, int rightEdge, int height)
{
    using mu_ui::s;
    using mu_ui::sf;
    const juce::Font font(juce::FontOptions{}.withHeight(sf(11.0f)));
    juce::GlyphArrangement ga;
    ga.addLineOfText(font, text, 0.0f, 0.0f);
    const int textW = (int) std::ceil(ga.getBoundingBox(0, ga.getNumGlyphs(), true).getWidth());
    const int w = textW + s(16);   // padding either side keeps the click target forgiving
    return { rightEdge - w, 0, w, height };
}

// The Support link sits at the bar's right edge.
juce::Rectangle<int> StatusBar::supportLinkBounds() const
{
    return linkBoundsFor("Support", getWidth() - mu_ui::s(4), getHeight());
}

// The update link sits just left of the Support link, or at the right edge without one.
juce::Rectangle<int> StatusBar::updateLinkBounds() const
{
    const int right = supportLinkVisible ? supportLinkBounds().getX() : getWidth() - mu_ui::s(4);
    return linkBoundsFor("Update available", right, getHeight());
}

void StatusBar::setUpdateLinkVisible(bool visible, const juce::String& tooltip)
{
    updateTooltip = tooltip;
    if (updateLinkVisible == visible) return;
    updateLinkVisible = visible;
    updateLinkHover   = false;
    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaint();
}

void StatusBar::mouseMove(const juce::MouseEvent& e)
{
    const bool over   = supportLinkVisible && supportLinkBounds().contains(e.getPosition());
    const bool overUp = updateLinkVisible  && updateLinkBounds().contains(e.getPosition());
    if (over != supportLinkHover || overUp != updateLinkHover)
    {
        supportLinkHover = over;
        updateLinkHover  = overUp;
        setMouseCursor(over || overUp ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void StatusBar::mouseExit(const juce::MouseEvent&)
{
    if (! supportLinkHover && ! updateLinkHover) return;
    supportLinkHover = false;
    updateLinkHover  = false;
    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaint();
}

void StatusBar::mouseDown(const juce::MouseEvent& e)
{
    if (updateLinkVisible && updateLinkBounds().contains(e.getPosition()))
    {
        auto& cb = e.mods.isPopupMenu() ? onUpdateHideRequested : onUpdateClicked;
        if (cb) cb();
        return;
    }
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

    // Update link: plain text, underlined on hover.
    if (updateLinkVisible)
    {
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(11.0f))).withStyle(updateLinkHover ? juce::Font::underlined : juce::Font::plain));
        g.setColour(MuLookAndFeel::colour(Id::statusBarValue));
        g.drawText("Update available", updateLinkBounds(), juce::Justification::centred, false);
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
    // The parameter text never runs under the links at the right edge: it is cut with an ellipsis there.
    const int rightLimit = (updateLinkVisible ? updateLinkBounds().getX()
                                              : supportLinkVisible ? supportLinkBounds().getX() : getWidth()) - s(8);
    const float nameW = measureW(currentName);
    const float sepW  = measureW("  ");
    const float valW  = measureW(currentValue);
    const int   startX = juce::jmax(textX, (int)(((float)getWidth() - nameW - sepW - valW) * 0.5f));

    g.setColour(MuLookAndFeel::colour(Id::statusBarText));
    const int nameX = startX;
    const int valX  = startX + (int) std::ceil(nameW + sepW);
    g.drawText(currentName,  nameX, 0, juce::jlimit(0, (int) std::ceil(nameW), rightLimit - nameX), h,
               juce::Justification::centredLeft, true);
    g.setColour(MuLookAndFeel::colour(Id::statusBarValue));
    g.drawText(currentValue, valX,  0, juce::jlimit(0, (int) std::ceil(valW) + 1, rightLimit - valX), h,
               juce::Justification::centredLeft, true);
}
