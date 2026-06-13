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

void StatusBar::paint(juce::Graphics& g)
{
    using Id = MuLookAndFeel::ColourIds;
    using mu_ui::s;
    using mu_ui::sf;

    g.setColour(MuLookAndFeel::colour(Id::statusBarBackground));
    g.fillAll();

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
