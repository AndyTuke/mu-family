#include "AddButton.h"

AddButton::AddButton(const juce::String& label) : juce::TextButton("+ " + label)
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void AddButton::paintButton(juce::Graphics& g, bool isOver, bool isDown)
{
    // Metal: the family button (background + label from the LookAndFeel), like New / Save.
    if (MuLookAndFeel::isMetal(*this))
    {
        juce::TextButton::paintButton(g, isOver, isDown);
        return;
    }

    // Flat: a dashed border, filled on hover.
    using Id = MuLookAndFeel::ColourIds;
    using mu_ui::sf;
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);

    if (isOver && isEnabled())
    {
        g.setColour(MuLookAndFeel::colour(Id::addButtonHoverBg));
        g.fillRoundedRectangle(bounds, sf(3.0f));
    }

    const float dashLen = sf(4.0f), gapLen = sf(3.0f);
    juce::Path border;
    border.addRoundedRectangle(bounds, sf(3.0f));
    g.setColour(MuLookAndFeel::colour(Id::addButtonBorder));
    juce::PathStrokeType stroke(1.0f);
    float dashes[] = { dashLen, gapLen };
    stroke.createDashedStroke(border, border, dashes, 2);
    g.strokePath(border, stroke);

    g.setColour(MuLookAndFeel::colour(Id::addButtonText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(11.0f))));
    g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred, true);
}

void AddButton::enablementChanged()
{
    setMouseCursor(isEnabled() ? juce::MouseCursor::PointingHandCursor
                               : juce::MouseCursor::NormalCursor);
    setAlpha(isEnabled() ? 1.0f : 0.35f);   // reads as inactive in both styles
    juce::TextButton::enablementChanged();
}
