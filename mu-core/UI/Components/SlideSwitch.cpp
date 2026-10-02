#include "SlideSwitch.h"

SlideSwitch::SlideSwitch(juce::String top, juce::String bottom, MuLookAndFeel::ColourIds accent)
    : topLabel(std::move(top)), bottomLabel(std::move(bottom)), accentId(accent)
{
    setRepaintsOnMouseActivity(true);
}

// Programmatic changes (preset load, host automation) snap straight to the end.
void SlideSwitch::setSelectedIndex(int index, bool notify)
{
    index = juce::jlimit(0, 1, index);
    if (index == selectedIndex) return;

    selectedIndex = index;
    position = (float) index;
    stopTimer();
    repaint();
    if (notify && onChange) onChange(selectedIndex);
}

void SlideSwitch::select(int index)
{
    if (index == selectedIndex) return;
    selectedIndex = index;
    startTimerHz(60);
    if (onChange) onChange(selectedIndex);
}

void SlideSwitch::paint(juce::Graphics& g)
{
    if (auto* mlf = dynamic_cast<MuLookAndFeel*>(&getLookAndFeel()))
        mlf->drawSlideSwitch(g, getLocalBounds().toFloat(), position,
                             MuLookAndFeel::colour(accentId), topLabel, bottomLabel,
                             selectedIndex, isMouseOverOrDragging());
}

void SlideSwitch::mouseDown(const juce::MouseEvent& e)
{
    dragStartY = e.position.y;
    dragged    = false;
}

// A drag only counts once it has moved a few pixels, so a slightly shaky click still flips.
void SlideSwitch::mouseDrag(const juce::MouseEvent& e)
{
    if (! dragged && std::abs(e.position.y - dragStartY) < mu_ui::sf(3.0f)) return;
    dragged = true;
    select(e.position.y > (float) getHeight() * 0.5f ? 1 : 0);
}

void SlideSwitch::mouseUp(const juce::MouseEvent&)
{
    if (! dragged) select(1 - selectedIndex);
}

// Ease the disc toward the selected end.
void SlideSwitch::timerCallback()
{
    const float target = (float) selectedIndex;
    position += (target - position) * 0.35f;
    if (std::abs(target - position) < 0.01f)
    {
        position = target;
        stopTimer();
    }
    repaint();
}
