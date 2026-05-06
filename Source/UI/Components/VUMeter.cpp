#include "VUMeter.h"
#include <cmath>

VUMeter::VUMeter()  { startTimerHz(30); }
VUMeter::~VUMeter() { stopTimer(); }

float VUMeter::linToDb(float lin) noexcept
{
    return lin > 1e-6f ? 20.0f * std::log10(lin) : kFloorDb;
}

float VUMeter::dbToNorm(float db) noexcept
{
    // Map kFloorDb..0 dBFS → 0..1 linearly across the visible meter height.
    const float range = -kFloorDb;
    return juce::jlimit(0.0f, 1.0f, (db - kFloorDb) / range);
}

void VUMeter::timerCallback()
{
    const float incoming = getLevel ? getLevel() : 0.0f;
    const float inDb     = linToDb(incoming);

    const float prevDisplay = displayDb;
    const float prevPeak    = peakDb;
    const bool  prevClip    = clipLit;

    // Attack: instant if louder, dB-domain release otherwise.
    displayDb = (inDb >= displayDb) ? inDb
                                    : juce::jmax(kFloorDb, displayDb + kReleasePerTick);

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
        peakDb = juce::jmax(kFloorDb, peakDb + kPeakDecayTick);
    }

    // Clip indicator latches when audio reaches/exceeds 0 dBFS.
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

    // Skip repaint when fully idle — saves 30 Hz redraws per meter.
    if (displayDb != prevDisplay || peakDb != prevPeak || clipLit != prevClip)
        repaint();
}

void VUMeter::mouseDown(const juce::MouseEvent&)
{
    // Click clears latched clip indicator.
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

    // Pre-compute zone boundaries in pixel space (y grows down, so higher dB → smaller y).
    const float yellowY = h - dbToNorm(kZeroVuDb)    * h;   // green→yellow at 0 VU (-18 dBFS)
    const float redY    = h - dbToNorm(kRedThreshDb) * h;   // yellow→red at -6 dBFS

    // Graduated level bar: green from bar top → yellowY, yellow from yellowY → redY,
    // red from redY → bar peak. Each segment only draws if the bar reaches it.
    const float barNorm = dbToNorm(displayDb);
    if (barNorm > 0.001f)
    {
        const float barTopY = h - barNorm * h;

        // Green segment (always at bottom of bar, capped by yellow boundary or bar top)
        const float greenTop = juce::jmax(barTopY, yellowY);
        if (greenTop < h)
        {
            g.setColour(juce::Colour(0xff44cc44));
            g.fillRect(0.0f, greenTop, w, h - greenTop);
        }

        // Yellow segment
        if (barTopY < yellowY)
        {
            const float ytop = juce::jmax(barTopY, redY);
            if (ytop < yellowY)
            {
                g.setColour(juce::Colour(0xffffcc00));
                g.fillRect(0.0f, ytop, w, yellowY - ytop);
            }
        }

        // Red segment
        if (barTopY < redY)
        {
            g.setColour(juce::Colour(0xffff3333));
            g.fillRect(0.0f, barTopY, w, redY - barTopY);
        }
    }

    // 0 VU reference tick mark (-18 dBFS) — thin white line across the meter.
    {
        g.setColour(juce::Colours::white.withAlpha(0.45f));
        g.fillRect(0.0f, yellowY, w, 1.0f);
    }

    // Peak hold tick — white normally, red when in red zone.
    if (peakDb > kFloorDb + 1.0f)
    {
        const float peakNorm = dbToNorm(peakDb);
        const int   peakY    = juce::jmax(0, (int)(h - peakNorm * h) - 1);
        g.setColour(peakDb >= kRedThreshDb ? juce::Colour(0xffff3333)
                                           : juce::Colours::white.withAlpha(0.85f));
        g.fillRect(0, peakY, (int)w, 1);
    }

    // Clip indicator (red strip at top, latched until clicked)
    if (clipLit)
    {
        g.setColour(juce::Colour(0xffff0000));
        g.fillRect(0.0f, 0.0f, w, 3.0f);
    }

    // Border
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.drawRoundedRectangle(0.0f, 0.0f, w, h, 2.0f, 1.0f);
}
