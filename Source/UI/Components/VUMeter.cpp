#include "VUMeter.h"
#include <cmath>

VUMeter::VUMeter()  { startTimerHz(30); }
VUMeter::~VUMeter() { stopTimer(); }

float VUMeter::linToDB(float lin) noexcept
{
    return lin > 1e-7f ? 20.0f * std::log10(lin) : kFloor;
}

float VUMeter::dbToNorm(float db) noexcept
{
    // Map -48dBFS..0dBFS → 0..1
    return juce::jlimit(0.0f, 1.0f, (db + 48.0f) / 48.0f);
}

void VUMeter::timerCallback()
{
    const float incoming = getLevel ? getLevel() : 0.0f;
    const float inDb     = linToDB(incoming);

    const float prevDisplay = displayDb;
    const float prevPeak    = peakDb;
    const bool  prevClip    = clipLit;

    // Attack: instant if louder
    displayDb = (inDb >= displayDb) ? inDb
                                    : juce::jmax(kFloor, displayDb + kReleasePerTick);

    // Peak hold / decay
    if (inDb >= peakDb)
    {
        peakDb   = inDb;
        peakHold = kPeakHoldFrames;
    }
    else if (peakHold > 0)
    {
        --peakHold;
    }
    else
    {
        peakDb = juce::jmax(kFloor, peakDb + kPeakDecayTick);
    }

    // Clip indicator
    if (incoming >= 1.0f)
    {
        clipLit  = true;
        clipHold = kClipHoldFrames;
    }
    else if (clipHold > 0)
    {
        --clipHold;
        if (clipHold == 0) clipLit = false;
    }

    // Skip repaint when fully idle (silent and steady) — saves 30Hz redraws per meter.
    if (displayDb != prevDisplay || peakDb != prevPeak || clipLit != prevClip)
        repaint();
}

void VUMeter::mouseDown(const juce::MouseEvent&)
{
    // Click clears clip indicator
    clipLit  = false;
    clipHold = 0;
    repaint();
}

void VUMeter::paint(juce::Graphics& g)
{
    const float w = (float)getWidth();
    const float h = (float)getHeight();

    // Background
    g.setColour(juce::Colour(0xff111111));
    g.fillRoundedRectangle(0.0f, 0.0f, w, h, 2.0f);

    // Level bar
    const float norm = dbToNorm(displayDb);
    if (norm > 0.001f)
    {
        const float barH = norm * h;
        const juce::Colour col = (norm > dbToNorm(-0.5f)) ? juce::Colour(0xffff3333)  // clip zone
                               : (norm > dbToNorm(-12.f)) ? juce::Colour(0xffffcc00)
                               :                            juce::Colour(0xff44cc44);
        g.setColour(col);
        g.fillRect(0.0f, h - barH, w, barH);
    }

    // Peak tick (green, unless in clip zone)
    if (peakDb > kFloor + 1.0f)
    {
        const float peakNorm = dbToNorm(peakDb);
        const int   peakY    = juce::jmax(0, (int)(h - peakNorm * h) - 1);
        g.setColour(peakDb >= -0.5f ? juce::Colour(0xffff3333)
                                    : juce::Colours::white.withAlpha(0.8f));
        g.fillRect(0, peakY, (int)w, 1);
    }

    // Clip indicator (red strip at top)
    if (clipLit)
    {
        g.setColour(juce::Colour(0xffff0000));
        g.fillRect(0.0f, 0.0f, w, 3.0f);
    }

    // Border
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.drawRoundedRectangle(0.0f, 0.0f, w, h, 2.0f, 1.0f);
}
