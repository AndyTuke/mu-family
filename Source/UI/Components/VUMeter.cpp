#include "VUMeter.h"

VUMeter::VUMeter()  { startTimerHz(60); }
VUMeter::~VUMeter() { stopTimer(); }

void VUMeter::timerCallback()
{
    const float incoming = getLevel ? getLevel() : 0.0f;

    displayLevel = (incoming >= displayLevel)
                 ? incoming
                 : juce::jmax(0.0f, displayLevel - kDecayPerTick);

    if (incoming >= peakLevel)
    {
        peakLevel     = incoming;
        peakHoldCount = kHoldFrames;
    }
    else if (peakHoldCount > 0)
    {
        --peakHoldCount;
    }
    else
    {
        peakLevel = juce::jmax(0.0f, peakLevel - kDecayPerTick * 0.5f);
    }

    repaint();
}

void VUMeter::paint(juce::Graphics& g)
{
    const float w = (float)getWidth();
    const float h = (float)getHeight();

    g.setColour(juce::Colour(0xff111111));
    g.fillRoundedRectangle(0.0f, 0.0f, w, h, 2.0f);

    if (displayLevel > 0.001f)
    {
        const float barH = displayLevel * h;
        const juce::Colour col = (displayLevel > 0.891f) ? juce::Colour(0xffff3333)
                               : (displayLevel > 0.631f) ? juce::Colour(0xffffcc00)
                               :                           juce::Colour(0xff44cc44);
        g.setColour(col);
        g.fillRect(0.0f, h - barH, w, barH);
    }

    if (peakLevel > 0.001f)
    {
        const int peakY = juce::jmax(0, (int)(h - peakLevel * h) - 1);
        g.setColour(juce::Colours::white.withAlpha(0.8f));
        g.fillRect(0, peakY, (int)w, 1);
    }

    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.drawRoundedRectangle(0.0f, 0.0f, w, h, 2.0f, 1.0f);
}
