#include "VUMeter.h"
#include "MuLookAndFeel.h"
#include <cmath>

VUMeter::VUMeter()  { startTimerHz(30); }
VUMeter::~VUMeter() { stopTimer(); }

float VUMeter::linToDb(float lin) noexcept
{
    return lin > 1e-6f ? 20.0f * std::log10(lin) : kFloorDb;
}

float VUMeter::dbToNorm(float db) noexcept
{
    return juce::jlimit(0.0f, 1.0f, (db - kFloorDb) / -kFloorDb);
}

float VUMeter::refDb() const noexcept
{
    if (mode == MeterMode::K12) return -12.0f;
    if (mode == MeterMode::K14) return -14.0f;
    return -18.0f;  // Peak and VU: 0 VU = −18 dBFS
}

float VUMeter::redDb() const noexcept
{
    if (mode == MeterMode::K12) return  -8.0f;   // +4 K above 0K
    if (mode == MeterMode::K14) return -10.0f;   // +4 K above 0K
    return -6.0f;   // Peak and VU
}

void VUMeter::timerCallback()
{
    const float incoming = getLevel ? getLevel() : 0.0f;
    const float inDb     = linToDb(incoming);

    const float prevDisplay = displayDb;
    const float prevPeak    = peakDb;
    const bool  prevClip    = clipLit;

    if (mode == MeterMode::Peak)
    {
        // Instant attack, dB-domain release
        displayDb = (inDb >= displayDb) ? inDb
                                        : juce::jmax(kFloorDb, displayDb + kReleasePerTick);

        // Peak hold / decay
        if (inDb >= peakDb) { peakDb = inDb; peakHold = kPeakHoldFrames; }
        else if (peakHold > 0) { --peakHold; }
        else { peakDb = juce::jmax(kFloorDb, peakDb + kPeakDecayTick); }
    }
    else
    {
        // VU / K-12 / K-14: symmetric IIR, 300 ms time constant
        const float target = juce::jmax(kFloorDb, inDb);
        displayDb = kVuAlpha * displayDb + (1.0f - kVuAlpha) * target;
        displayDb = juce::jmax(kFloorDb, displayDb);
        peakDb    = kFloorDb;   // hide peak tick in VU/K modes
    }

    // Clip indicator (all modes)
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

    if (displayDb != prevDisplay || peakDb != prevPeak || clipLit != prevClip)
        repaint();
}

void VUMeter::mouseDown(const juce::MouseEvent&)
{
    clipLit  = false;
    clipHold = 0;
    repaint();
}

void VUMeter::paint(juce::Graphics& g)
{
    const float w = (float)getWidth();
    const float h = (float)getHeight();

    using Id = MuLookAndFeel::ColourIds;
    g.setColour(MuLookAndFeel::colour(Id::vuMeterBackground));
    g.fillRoundedRectangle(0.0f, 0.0f, w, h, 2.0f);

    const float ref      = refDb();
    const float red      = redDb();
    const float yellowY  = h - dbToNorm(ref) * h;
    const float redY     = h - dbToNorm(red) * h;

    const float barNorm = dbToNorm(displayDb);
    if (barNorm > 0.001f)
    {
        const float barTopY = h - barNorm * h;

        // Green segment
        const float greenTop = juce::jmax(barTopY, yellowY);
        if (greenTop < h)
        {
            g.setColour(MuLookAndFeel::colour(Id::vuMeterGreen));
            g.fillRect(0.0f, greenTop, w, h - greenTop);
        }

        // Yellow segment
        if (barTopY < yellowY)
        {
            const float ytop = juce::jmax(barTopY, redY);
            if (ytop < yellowY)
            {
                g.setColour(MuLookAndFeel::colour(Id::vuMeterYellow));
                g.fillRect(0.0f, ytop, w, yellowY - ytop);
            }
        }

        // Red segment
        if (barTopY < redY)
        {
            g.setColour(MuLookAndFeel::colour(Id::vuMeterRed));
            g.fillRect(0.0f, barTopY, w, redY - barTopY);
        }
    }

    // Reference mark (0 VU / 0K)
    g.setColour(juce::Colours::white.withAlpha(0.45f));
    g.fillRect(0.0f, yellowY, w, 1.0f);

    // Peak-hold tick (Peak mode only — peakDb is kFloorDb in other modes)
    if (peakDb > kFloorDb + 1.0f)
    {
        const float peakNorm = dbToNorm(peakDb);
        const int   peakY    = juce::jmax(0, (int)(h - peakNorm * h) - 1);
        g.setColour(peakDb >= red ? MuLookAndFeel::colour(Id::vuMeterRed)
                                  : juce::Colours::white.withAlpha(0.85f));
        g.fillRect(0, peakY, (int)w, 1);
    }

    // Clip strip
    if (clipLit)
    {
        g.setColour(MuLookAndFeel::colour(Id::vuMeterClipFlash));
        g.fillRect(0.0f, 0.0f, w, 3.0f);
    }

    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.drawRoundedRectangle(0.0f, 0.0f, w, h, 2.0f, 1.0f);
}
