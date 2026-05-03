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
    using Id = MuClidLookAndFeel::ColourIds;

    g.setColour(MuClidLookAndFeel::colour(Id::statusBarBackground));
    g.fillAll();

    const int h = getHeight();
    int textX = 8;

    // Rhythm colour tag
    if (tagColour.getAlpha() > 0)
    {
        g.setColour(tagColour);
        g.fillRect(0, 0, kTagWidth, h);
        textX += kTagWidth + 4;
    }

    g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));

    // Param name
    g.setColour(MuClidLookAndFeel::colour(Id::statusBarText));
    g.drawText(currentName, textX, 0, getWidth() / 2, h,
               juce::Justification::centredLeft, true);

    // Value
    g.setColour(MuClidLookAndFeel::colour(Id::statusBarValue));
    g.drawText(currentValue, textX + getWidth() / 2 - textX, 0,
               getWidth() / 2, h, juce::Justification::centredLeft, true);
}
