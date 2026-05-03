#include "AddButton.h"

AddButton::AddButton(const juce::String& label) : labelText(label)
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void AddButton::paint(juce::Graphics& g)
{
    using Id = MuClidLookAndFeel::ColourIds;

    auto bounds = getLocalBounds().toFloat().reduced(0.5f);

    if (hovered)
    {
        g.setColour(MuClidLookAndFeel::colour(Id::addButtonHoverBg));
        g.fillRoundedRectangle(bounds, 3.0f);
    }

    // Dashed border
    const float dashLen = 4.0f, gapLen = 3.0f;
    juce::Path border;
    border.addRoundedRectangle(bounds, 3.0f);
    g.setColour(MuClidLookAndFeel::colour(Id::addButtonBorder));
    juce::PathStrokeType stroke(1.0f);
    float dashes[] = { dashLen, gapLen };
    stroke.createDashedStroke(border, border, dashes, 2);
    g.strokePath(border, stroke);

    g.setColour(MuClidLookAndFeel::colour(Id::addButtonText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    g.drawText("+ " + labelText, getLocalBounds(), juce::Justification::centred, true);
}

void AddButton::mouseDown(const juce::MouseEvent&)
{
    if (onClick) onClick();
}

void AddButton::mouseEnter(const juce::MouseEvent&)
{
    hovered = true;
    repaint();
}

void AddButton::mouseExit(const juce::MouseEvent&)
{
    hovered = false;
    repaint();
}
