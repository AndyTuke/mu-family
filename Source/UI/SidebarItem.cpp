#include "SidebarItem.h"

SidebarItem::SidebarItem(int index)
    : rhythmIndex(index)
{
    addAndMakeVisible(miniCircle);
    miniCircle.setInterceptsMouseClicks(false, false);
    startTimerHz(30);
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

void SidebarItem::setPlayState(PluginProcessor::RhythmPlayState* state,
                                const juce::Atomic<float>*         beatFrac,
                                const juce::Atomic<bool>*           playing)
{
    playState = state;
    miniCircle.setPlayState(state, beatFrac, playing, rhythmColour);
}

void SidebarItem::timerCallback()
{
    // Detect hit — read the flag here before RhythmCircle clears it.
    // Both read in the same timer window; the race is benign (animation only).
    if (playState && playState->hitFired.get())
        pulseAlpha = 0.4f;

    if (pulseAlpha > 0.0f)
    {
        // Quadratic ease-out decay over ~200ms (6 ticks at 30Hz)
        pulseAlpha = juce::jmax(0.0f, pulseAlpha - (0.4f / (0.2f * 30.0f)));
        repaint();
    }
}

void SidebarItem::resized()
{
    const int w          = getWidth();
    const int nameH      = 14;
    const int circleSize = juce::jmin(w - 8, getHeight() - nameH - 6);
    const int circleX    = (w - circleSize) / 2;
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

    // Background hit pulse (drawn over base bg, under everything else)
    if (pulseAlpha > 0.0f)
    {
        g.setColour(rhythmColour.withAlpha(pulseAlpha));
        g.fillRect(0, 0, w, h);
    }

    // Right-edge tab line when selected
    if (selected)
    {
        g.setColour(rhythmColour);
        g.fillRect(w - 3, 0, 3, h);
    }

    // Name row below circle
    const int nameY = miniCircle.getBottom() + 2;
    const int nameRowH = h - nameY - 2;
    if (nameRowH > 0)
    {
        g.setColour(rhythmColour);
        g.fillEllipse(5.0f, (float)(nameY + (nameRowH - 6) / 2), 6.0f, 6.0f);

        const juce::String name = rhythm ? juce::String(rhythm->name) : "---";
        g.setColour(MuClidLookAndFeel::colour(selected ? Id::valueText : Id::labelText));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.0f)));
        g.drawText(name, 14, nameY, w - 18, nameRowH, juce::Justification::centredLeft, true);
    }

    // Bottom separator
    g.setColour(MuClidLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawLine(4.0f, (float)(h - 1), (float)(w - 4), (float)(h - 1), 0.5f);
}

void SidebarItem::mouseDown(const juce::MouseEvent&)
{
    if (onSelected) onSelected(rhythmIndex);
}
