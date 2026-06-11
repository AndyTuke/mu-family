#include "AddButton.h"

AddButton::AddButton(const juce::String& label) : labelText(label)
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void AddButton::paint(juce::Graphics& g)
{
    using Id = MuLookAndFeel::ColourIds;
    using mu_ui::sf;

    auto bounds = getLocalBounds().toFloat().reduced(0.5f);

    // Disabled = demo cap reached (or no add wired): dim so it reads as inactive.
    const float a = isEnabled() ? 1.0f : 0.35f;

    if (hovered && isEnabled())
    {
        g.setColour(MuLookAndFeel::colour(Id::addButtonHoverBg));
        g.fillRoundedRectangle(bounds, sf(3.0f));
    }

    // Dashed border
    const float dashLen = sf(4.0f), gapLen = sf(3.0f);
    juce::Path border;
    border.addRoundedRectangle(bounds, sf(3.0f));
    g.setColour(MuLookAndFeel::colour(Id::addButtonBorder).withMultipliedAlpha(a));
    juce::PathStrokeType stroke(1.0f);
    float dashes[] = { dashLen, gapLen };
    stroke.createDashedStroke(border, border, dashes, 2);
    g.strokePath(border, stroke);

    g.setColour(MuLookAndFeel::colour(Id::addButtonText).withMultipliedAlpha(a));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(11.0f))));
    g.drawText("+ " + labelText, getLocalBounds(), juce::Justification::centred, true);
}

void AddButton::mouseDown(const juce::MouseEvent&)
{
    if (! isEnabled()) return;   // demo cap reached — no-op
    if (onClick) onClick();
}

void AddButton::mouseEnter(const juce::MouseEvent&)
{
    if (! isEnabled()) return;
    hovered = true;
    repaint();
}

void AddButton::mouseExit(const juce::MouseEvent&)
{
    hovered = false;
    repaint();
}

void AddButton::enablementChanged()
{
    setMouseCursor(isEnabled() ? juce::MouseCursor::PointingHandCursor
                               : juce::MouseCursor::NormalCursor);
    if (! isEnabled()) hovered = false;
    repaint();
}
