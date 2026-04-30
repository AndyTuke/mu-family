#include "SidebarItem.h"

SidebarItem::SidebarItem(int index)
    : rhythmIndex(index)
{
    addAndMakeVisible(miniCircle);
}

void SidebarItem::setRhythm(const Rhythm* r, juce::Colour colour)
{
    rhythm = r;
    rhythmColour = colour;
    miniCircle.setPatterns(r ? r->genA.getStepTypes() : std::vector<StepType>{},
                           r ? r->genB.getStepTypes() : std::vector<StepType>{},
                           r ? r->genC.getStepTypes() : std::vector<StepType>{});
    repaint();
}

void SidebarItem::setSelected(bool s)
{
    selected = s;
    repaint();
}

void SidebarItem::pulse()
{
    miniCircle.pulseA();
}

void SidebarItem::resized()
{
    const int w = getWidth();
    const int nameH = 14;
    const int circleSize = juce::jmin(w - 8, getHeight() - nameH - 6);
    const int circleX = (w - circleSize) / 2;
    miniCircle.setBounds(circleX, 4, circleSize, circleSize);
}

void SidebarItem::paint(juce::Graphics& g)
{
    using Id = MuClidLookAndFeel::ColourIds;

    const int w = getWidth();
    const int h = getHeight();

    g.setColour(MuClidLookAndFeel::colour(
        selected ? Id::sidebarItemSelected : Id::sidebarItemBackground));
    g.fillRect(0, 0, w, h);

    // Right-edge tab line when selected
    if (selected)
    {
        g.setColour(rhythmColour);
        g.fillRect(w - 3, 0, 3, h);
    }

    // Name row below circle
    const int nameY = miniCircle.getBottom() + 2;
    const int nameH = h - nameY - 2;
    if (nameH > 0)
    {
        g.setColour(rhythmColour);
        g.fillEllipse(5.0f, (float)(nameY + (nameH - 6) / 2), 6.0f, 6.0f);

        const juce::String name = rhythm ? juce::String(rhythm->name) : "---";
        g.setColour(MuClidLookAndFeel::colour(selected ? Id::valueText : Id::labelText));
        g.setFont(juce::Font(9.0f));
        g.drawText(name, 14, nameY, w - 18, nameH, juce::Justification::centredLeft, true);
    }

    // Bottom separator
    g.setColour(MuClidLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawLine(4.0f, (float)(h - 1), (float)(w - 4), (float)(h - 1), 0.5f);
}

void SidebarItem::mouseDown(const juce::MouseEvent&)
{
    if (onSelected) onSelected(rhythmIndex);
}
