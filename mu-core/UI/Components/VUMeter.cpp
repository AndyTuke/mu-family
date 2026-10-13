#include "VUMeter.h"
#include "MuLookAndFeel.h"
#include <cmath>

VUMeter::VUMeter()  { startTimerHz(mu_ui::kUiRefreshHz); }
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

bool VUMeter::tick(Bar& b, float incoming) const noexcept
{
    const float inDb = linToDb(incoming);

    const float prevDisplay = b.displayDb;
    const float prevPeak    = b.peakDb;
    const bool  prevClip    = b.clipLit;

    if (mode == MeterMode::Peak)
    {
        // Instant attack, dB-domain release
        b.displayDb = (inDb >= b.displayDb) ? inDb
                                            : juce::jmax(kFloorDb, b.displayDb + kReleasePerTick);

        // Peak hold / decay
        if (inDb >= b.peakDb) { b.peakDb = inDb; b.peakHold = kPeakHoldFrames; }
        else if (b.peakHold > 0) { --b.peakHold; }
        else { b.peakDb = juce::jmax(kFloorDb, b.peakDb + kPeakDecayTick); }
    }
    else
    {
        // VU / K-12 / K-14: symmetric IIR, 300 ms time constant
        const float target = juce::jmax(kFloorDb, inDb);
        b.displayDb = kVuAlpha * b.displayDb + (1.0f - kVuAlpha) * target;
        b.displayDb = juce::jmax(kFloorDb, b.displayDb);
        b.peakDb    = kFloorDb;   // hide peak tick in VU/K modes
    }

    // Clip indicator (all modes)
    if (incoming >= 1.0f)
    {
        b.clipLit  = true;
        b.clipHold = kClipHoldFrames;
    }
    else if (b.clipHold > 0)
    {
        --b.clipHold;
        if (b.clipHold == 0) b.clipLit = false;
    }

    return b.displayDb != prevDisplay || b.peakDb != prevPeak || b.clipLit != prevClip;
}

void VUMeter::timerCallback()
{
    if (! isShowing()) return;   // mixer hidden — no ballistics or repaints until it is shown
    const float left  = getLevel ? getLevel() : 0.0f;
    const float right = getLevelR ? getLevelR() : left;   // a mono source drives both bars

    const bool changedL = tick(bars[0], left);
    const bool changedR = tick(bars[1], right);
    if (changedL || changedR)
        repaint();
}

void VUMeter::mouseDown(const juce::MouseEvent&)
{
    for (auto& b : bars) { b.clipLit = false; b.clipHold = 0; }
    repaint();
}

void VUMeter::paint(juce::Graphics& g)
{
    // Two bars side by side, kVUBarGap apart; an odd pixel goes to the right bar.
    const auto  all = getLocalBounds().toFloat();
    const float gap = (float) mu_ui::s(kBarGap);
    const float lw  = std::floor((all.getWidth() - gap) * 0.5f);
    paintBar(g, bars[0], all.withWidth(lw));
    paintBar(g, bars[1], all.withTrimmedLeft(lw + gap));
}

void VUMeter::paintBar(juce::Graphics& g, const Bar& b, juce::Rectangle<float> r) const
{
    using mu_ui::sf;
    const float x = r.getX();
    const float w = r.getWidth();
    const float h = r.getHeight();

    using Id = MuLookAndFeel::ColourIds;
    g.setColour(MuLookAndFeel::colour(Id::vuMeterBackground));
    g.fillRoundedRectangle(x, 0.0f, w, h, sf(2.0f));

    const float ref      = refDb();
    const float red      = redDb();
    const float yellowY  = h - dbToNorm(ref) * h;
    const float redY     = h - dbToNorm(red) * h;

    const float barNorm = dbToNorm(b.displayDb);
    if (barNorm > 0.001f)
    {
        const float barTopY = h - barNorm * h;

        // Green segment
        const float greenTop = juce::jmax(barTopY, yellowY);
        if (greenTop < h)
        {
            g.setColour(MuLookAndFeel::colour(Id::vuMeterGreen));
            g.fillRect(x, greenTop, w, h - greenTop);
        }

        // Yellow segment
        if (barTopY < yellowY)
        {
            const float ytop = juce::jmax(barTopY, redY);
            if (ytop < yellowY)
            {
                g.setColour(MuLookAndFeel::colour(Id::vuMeterYellow));
                g.fillRect(x, ytop, w, yellowY - ytop);
            }
        }

        // Red segment
        if (barTopY < redY)
        {
            g.setColour(MuLookAndFeel::colour(Id::vuMeterRed));
            g.fillRect(x, barTopY, w, redY - barTopY);
        }
    }

    // Reference mark (0 VU / 0K)
    g.setColour(juce::Colours::white.withAlpha(0.45f));
    g.fillRect(x, yellowY, w, 1.0f);

    // Peak-hold tick (Peak mode only — peakDb is kFloorDb in other modes)
    if (b.peakDb > kFloorDb + 1.0f)
    {
        const float peakNorm = dbToNorm(b.peakDb);
        const float peakY    = juce::jmax(0.0f, std::floor(h - peakNorm * h) - 1.0f);
        g.setColour(b.peakDb >= red ? MuLookAndFeel::colour(Id::vuMeterRed)
                                    : juce::Colours::white.withAlpha(0.85f));
        g.fillRect(x, peakY, w, 1.0f);
    }

    // Clip strip
    if (b.clipLit)
    {
        g.setColour(MuLookAndFeel::colour(Id::vuMeterClipFlash));
        g.fillRect(x, 0.0f, w, sf(3.0f));
    }

    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.drawRoundedRectangle(x, 0.0f, w, h, sf(2.0f), 1.0f);
}
