// Host meter: readHostTransport picks up the host's time signature and bar start, and
// barPositionOf turns a beat into the transport bar's bar.beat.sixteenth display in any meter.

#include <juce_audio_processors/juce_audio_processors.h>
#include "Plugin/HostTransport.h"

class BarPositionTest : public juce::UnitTest
{
public:
    BarPositionTest() : juce::UnitTest ("Bar position", "Transport") {}

    struct MeterPlayHead : juce::AudioPlayHead
    {
        int num = 4, den = 4;
        double ppq = 0.0, barStart = -1.0;   // < 0 = no bar start supplied
        bool giveSig = true;

        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setIsPlaying(true);
            info.setPpqPosition(ppq);
            if (giveSig) info.setTimeSignature(juce::AudioPlayHead::TimeSignature { num, den });
            if (barStart >= 0.0) info.setPpqPositionOfLastBarStart(barStart);
            return info;
        }
    };

    void expectPos(const mu_core::BarPosition& p, int bar, int beat, int sub, const juce::String& what)
    {
        expectEquals (p.bar,  bar,  what + " bar");
        expectEquals (p.beat, beat, what + " beat");
        expectEquals (p.sub,  sub,  what + " sixteenth");
    }

    void runTest() override
    {
        beginTest ("readHostTransport forwards the meter and bar start; defaults to 4/4");
        {
            MeterPlayHead ph;
            ph.num = 7; ph.den = 8; ph.ppq = 5.0; ph.barStart = 3.5;
            auto t = mu_core::readHostTransport(&ph);
            expectEquals (t.timeSigNumerator, 7);
            expectEquals (t.timeSigDenominator, 8);
            expect (t.hasBarStart);
            expectEquals (t.barStartPpq, 3.5);

            ph.giveSig = false; ph.barStart = -1.0;
            t = mu_core::readHostTransport(&ph);
            expectEquals (t.timeSigNumerator, 4);
            expectEquals (t.timeSigDenominator, 4);
            expect (! t.hasBarStart);
        }

        beginTest ("4/4 with no bar start matches the old fixed grid");
        {
            expectPos (mu_core::barPositionOf(0.0, 4, 4),   1, 1, 1, "start");
            expectPos (mu_core::barPositionOf(4.75, 4, 4),  2, 1, 4, "bar 2, last sixteenth of beat 1");
            expectPos (mu_core::barPositionOf(15.5, 4, 4),  4, 4, 3, "bar 4 beat 4");
            expectPos (mu_core::barPositionOf(0.25 - 1.0e-12, 4, 4), 1, 1, 2, "a rounding hair below a boundary");
        }

        beginTest ("3/4 and 7/8 bars");
        {
            expectPos (mu_core::barPositionOf(3.0, 3, 4),  2, 1, 1, "3/4: beat 3 starts bar 2");
            expectPos (mu_core::barPositionOf(5.5, 3, 4),  2, 3, 3, "3/4: bar 2 beat 3");
            // 7/8: a beat is an eighth (0.5 quarter notes, two sixteenths); a bar is 3.5 beats.
            expectPos (mu_core::barPositionOf(3.5, 7, 8),  2, 1, 1, "7/8: bar 2 downbeat");
            expectPos (mu_core::barPositionOf(6.75, 7, 8), 2, 7, 2, "7/8: bar 2 last eighth, second sixteenth");
        }

        beginTest ("A host bar start anchors the grid (pickup bar)");
        {
            // The song starts with a one-beat pickup, so bars begin at 1, 5, 9 … in 4/4.
            expectPos (mu_core::barPositionOf(5.0, 4, 4, true, 5.0), 2, 1, 1, "downbeat after the pickup");
            expectPos (mu_core::barPositionOf(7.5, 4, 4, true, 5.0), 2, 3, 3, "mid bar");
            expectPos (mu_core::barPositionOf(0.5, 4, 4, true, 1.0), 0, 4, 3, "inside the pickup");
        }
    }
};

static BarPositionTest barPositionTest;
