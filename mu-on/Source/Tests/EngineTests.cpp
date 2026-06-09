// Engine smoke tests: a triggered kick / hat produces non-silent output and then decays
// back to silence. (BassEngine pulls in the mu-core MultiModeFilter stack and is covered
// by the plugin build + the manual groove run rather than this console target.)

#include <juce_audio_basics/juce_audio_basics.h>
#include "Audio/KickEngine.h"
#include "Audio/SampleChannel.h"

using namespace mu_on;

namespace
{
    float rms(const juce::AudioBuffer<float>& b, int n)
    {
        double sum = 0.0;
        for (int c = 0; c < b.getNumChannels(); ++c)
            for (int i = 0; i < n; ++i) { const float v = b.getReadPointer(c)[i]; sum += (double) v * v; }
        return (float) std::sqrt(sum / juce::jmax(1, n * b.getNumChannels()));
    }

    // RMS over the sample range [from, to) on channel 0.
    float rmsRange(const juce::AudioBuffer<float>& b, int from, int to)
    {
        double sum = 0.0; const int n = juce::jmax(1, to - from);
        for (int i = from; i < to; ++i) { const float v = b.getReadPointer(0)[i]; sum += (double) v * v; }
        return (float) std::sqrt(sum / n);
    }
}

class EngineTest : public juce::UnitTest
{
public:
    EngineTest() : juce::UnitTest("mu-on Engines", "Engine") {}

    void runTest() override
    {
        constexpr double sr = 48000.0;
        constexpr int    n  = 256;

        beginTest("KickEngine: silent until triggered, then sounds and decays");
        {
            KickEngine kick;
            kick.prepare(sr);
            kick.setParams(50.0f, 220.0f, 50.0f, 120.0f, 0.3f);

            juce::AudioBuffer<float> buf(2, n);
            buf.clear(); kick.render(buf, n);
            expect(rms(buf, n) < 1.0e-7f, "no sound before a trigger");

            kick.trigger(1.0f);
            buf.clear(); kick.render(buf, n);
            expect(rms(buf, n) > 0.01f, "kick should sound on the block it is triggered");

            // Render ~1.5 s; the amp envelope must bring it down to effective silence (-60 dB).
            float tail = 1.0f;
            for (int b = 0; b < (int) (1.5 * sr / n); ++b) { buf.clear(); kick.render(buf, n); tail = rms(buf, n); }
            expect(tail < 1.0e-3f, "kick should decay to silence");
        }

        beginTest("SampleChannel (hat): silent until triggered, then sounds and decays");
        {
            SampleChannel hat;
            hat.prepare(sr, n, SampleChannel::HiHat);
            hat.setParams(0.0f, 60.0f);

            juce::AudioBuffer<float> buf(2, n);
            buf.clear(); hat.render(buf, n);
            expect(rms(buf, n) < 1.0e-7f, "no sound before a trigger");

            hat.trigger(1.0f);
            buf.clear(); hat.render(buf, n);
            expect(rms(buf, n) > 0.005f, "hat should sound on the block it is triggered");

            float tail = 1.0f;
            for (int b = 0; b < (int) (sr / n); ++b) { buf.clear(); hat.render(buf, n); tail = rms(buf, n); }
            expect(tail < 1.0e-3f, "hat should decay to silence");
        }

        beginTest("KickEngine: sample-accurate onset — silent before the offset, sounds after");
        {
            KickEngine kick;
            kick.prepare(sr);
            kick.setParams(50.0f, 220.0f, 50.0f, 120.0f, 0.3f);

            const int off = n / 2;
            kick.trigger(1.0f, off);
            juce::AudioBuffer<float> buf(2, n);
            buf.clear(); kick.render(buf, n);
            expect(rmsRange(buf, 0, off)  < 1.0e-7f, "silent before the onset sample");
            expect(rmsRange(buf, off, n)  > 0.01f,   "sounds from the onset sample on");
        }

        beginTest("SampleChannel: sample-accurate onset — silent before the offset, sounds after");
        {
            SampleChannel hat;
            hat.prepare(sr, n, SampleChannel::HiHat);
            hat.setParams(0.0f, 60.0f);

            const int off = n / 2;
            hat.trigger(1.0f, off);
            juce::AudioBuffer<float> buf(2, n);
            buf.clear(); hat.render(buf, n);
            expect(rmsRange(buf, 0, off) < 1.0e-7f, "silent before the onset sample");
            expect(rmsRange(buf, off, n) > 0.002f,  "sounds from the onset sample on");
        }

        beginTest("reset() silences an active engine (transport stop)");
        {
            KickEngine kick;
            kick.prepare(sr);
            kick.setParams(50.0f, 220.0f, 50.0f, 5000.0f, 0.0f);   // long decay so it would still ring

            juce::AudioBuffer<float> buf(2, n);
            kick.trigger(1.0f);
            buf.clear(); kick.render(buf, n);
            expect(rms(buf, n) > 0.01f, "kick is ringing");

            kick.reset();
            buf.clear(); kick.render(buf, n);
            expect(rms(buf, n) < 1.0e-7f, "reset() silences the voice immediately");
        }
    }
};

static EngineTest engineTest;
