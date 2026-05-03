#include "AboutPanel.h"
#include "../BuildNumber.h"

AboutPanel::AboutPanel()
{
    closeBtn.onClick = [this] { if (onDismiss) onDismiss(); };
    addAndMakeVisible(closeBtn);
}

void AboutPanel::mouseDown(const juce::MouseEvent& e)
{
    const int w = getWidth();
    const int h = getHeight();
    const int cardX = (w - kCardW) / 2;
    const int cardY = (h - kCardH) / 2;
    const juce::Rectangle<int> card { cardX, cardY, kCardW, kCardH };

    if (!card.contains(e.getPosition()))
        if (onDismiss) onDismiss();
}

void AboutPanel::resized()
{
    const int w = getWidth();
    const int h = getHeight();
    const int cardX = (w - kCardW) / 2;
    const int cardY = (h - kCardH) / 2;

    closeBtn.setBounds(cardX + kCardW / 2 - 36, cardY + kCardH - 40, 72, 28);
}

void AboutPanel::paint(juce::Graphics& g)
{
    using Id = MuClidLookAndFeel::ColourIds;

    // Dim background
    g.setColour(juce::Colour(0xe6000000));
    g.fillAll();

    const int w = getWidth();
    const int h = getHeight();
    const int cardX = (w - kCardW) / 2;
    const int cardY = (h - kCardH) / 2;

    // Card background
    g.setColour(MuClidLookAndFeel::colour(Id::panelBackground));
    g.fillRoundedRectangle((float)cardX, (float)cardY, (float)kCardW, (float)kCardH, 8.0f);

    g.setColour(MuClidLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawRoundedRectangle((float)cardX, (float)cardY, (float)kCardW, (float)kCardH, 8.0f, 1.0f);

    const int tx = cardX + 24;
    int ty = cardY + 24;

    // Title
    g.setColour(MuClidLookAndFeel::colour(Id::headingText));
    g.setFont(juce::Font(28.0f));
    g.drawText(juce::String(juce::CharPointer_UTF8("\xce\xbc")) + "-Clid",
               tx, ty, kCardW - 48, 36, juce::Justification::centredLeft, false);
    ty += 40;

    g.setFont(juce::Font(11.0f));
    g.setColour(MuClidLookAndFeel::colour(Id::mutedText));
    g.drawText("v0.0.0." + juce::String(BUILD_NUMBER), tx, ty, kCardW - 48, 18,
               juce::Justification::centredLeft, false);
    ty += 20;

    g.drawText("Transwarp Development Project", tx, ty, kCardW - 48, 18,
               juce::Justification::centredLeft, false);
    ty += 30;

    // Credits
    g.setColour(MuClidLookAndFeel::colour(Id::labelText));
    g.setFont(juce::Font(10.0f));
    const juce::String credits[] = {
        "JUCE — Proprietary (JUCE 7 license)",
        "SoundTouch — LGPL 2.1 (ships as DLL)",
        "Signalsmith Reverb — MIT",
        "Bjorklund algorithm — public domain",
    };
    for (auto& line : credits)
    {
        g.drawText(line, tx, ty, kCardW - 48, 16, juce::Justification::centredLeft, false);
        ty += 17;
    }
}
