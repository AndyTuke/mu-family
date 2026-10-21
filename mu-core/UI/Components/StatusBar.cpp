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

// The CPU readout's fixed slot at the extreme right edge, so its digits never move the text beside it.
juce::Rectangle<int> StatusBar::cpuBounds() const
{
    using mu_ui::s;
    return { getWidth() - s(MuLookAndFeel::kSpaceS) - s(MuLookAndFeel::kStatusCpuW), 0, s(MuLookAndFeel::kStatusCpuW), getHeight() };
}

int StatusBar::linksRightEdgeFor(int width, bool cpuVisible) noexcept
{
    using mu_ui::s;
    const int edge = width - s(4);
    return cpuVisible ? edge - (s(MuLookAndFeel::kStatusCpuW) + s(MuLookAndFeel::kSpaceS)) : edge;
}

// The Support link sits at the bar's right edge (left of the CPU readout when that is on).
juce::Rectangle<int> StatusBar::supportLinkBounds() const
{
    return linkBoundsFor("Support", linksRightEdgeFor(getWidth(), cpuVisible), getHeight());
}

// The update link sits just left of the Support link, or at the right edge without one.
juce::Rectangle<int> StatusBar::updateLinkBounds() const
{
    const int right = supportLinkVisible ? supportLinkBounds().getX() : linksRightEdgeFor(getWidth(), cpuVisible);
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

int StatusBar::cpuPercentFor(float load) noexcept
{
    return juce::jmax(0, juce::roundToInt(load * 100.0f));
}

// The band is read from the shown whole percent, so a number never turns amber while it still reads under the threshold.
StatusBar::CpuLevel StatusBar::cpuLevelFor(float load) noexcept
{
    const int pct = cpuPercentFor(load);
    if (pct >= juce::roundToInt(MuLookAndFeel::kCpuHotAt * 100.0f))  return CpuLevel::Hot;
    if (pct >= juce::roundToInt(MuLookAndFeel::kCpuWarnAt * 100.0f)) return CpuLevel::Warn;
    return CpuLevel::Normal;
}

void StatusBar::setCpuMeterVisible(bool visible)
{
    if (cpuVisible == visible) return;
    cpuVisible = visible;
    repaint();   // the links move with it
}

void StatusBar::setCpuLoad(float load)
{
    const int pct = cpuPercentFor(load);
    if (pct == cpuPercent) return;
    const auto now = juce::Time::getMillisecondCounter();
    if (cpuVisible && now - cpuPaintedAt < (juce::uint32) kCpuRepaintMs) return;   // the next call catches up
    cpuPercent   = pct;
    cpuPaintedAt = now;
    if (cpuVisible) repaint(cpuBounds());
}

juce::String StatusBar::getTooltip()
{
    if (updateLinkHover) return updateTooltip;
    if (cpuVisible && cpuBounds().contains(getMouseXYRelative()))
        return "Audio load: processing time as a share of the time available, peak of the last second.";
    return {};
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

    // CPU readout at the extreme right edge: "CPU" in the label colour, the number in the value colour, or amber / red
    // once the load is high. Right-aligned in its fixed slot.
    if (cpuVisible)
    {
        const juce::Font cpuFont(juce::FontOptions{}.withHeight(sf(11.0f)));
        g.setFont(cpuFont);
        auto width = [&cpuFont](const juce::String& t)
        {
            juce::GlyphArrangement ga;
            ga.addLineOfText(cpuFont, t, 0.0f, 0.0f);
            return (int) std::ceil(ga.getBoundingBox(0, ga.getNumGlyphs(), true).getWidth());
        };
        const auto slot = cpuBounds();
        const juce::String number = juce::String(cpuPercent) + "%", label = "CPU ";
        const int numberW = width(number), labelW = width(label);
        const int x = slot.getRight() - numberW;
        const float shown = (float) cpuPercent / 100.0f;
        const auto level = cpuLevelFor(shown);
        g.setColour(MuLookAndFeel::colour(Id::statusBarText));
        g.drawText(label, x - labelW, 0, labelW, slot.getHeight(), juce::Justification::centredRight, false);
        g.setColour(MuLookAndFeel::colour(level == CpuLevel::Hot ? Id::vuMeterClip : level == CpuLevel::Warn ? Id::vuMeterMid : Id::statusBarValue));
        g.drawText(number, x, 0, numberW + 1, slot.getHeight(), juce::Justification::centredLeft, false);
    }

    // Support link (unlicensed only), left of the CPU readout when that is on; drawn first so the parameter text,
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
                                              : supportLinkVisible ? supportLinkBounds().getX()
                                              : cpuVisible ? cpuBounds().getX() : getWidth()) - s(8);
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
