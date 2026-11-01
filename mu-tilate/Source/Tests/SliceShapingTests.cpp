// The slice player's per-slice shaping (SliceSettings): pan, pitch, filter, the amp envelope and loop.

#include "Audio/SlicePlayer.h"

#include <juce_core/juce_core.h>

using namespace mu_tilate;

namespace
{
constexpr double kSr = 48000.0;

juce::AudioBuffer<float> constant(int n, float value, int channels = 1)
{
    juce::AudioBuffer<float> b(channels, n);
    for (int c = 0; c < channels; ++c)
        for (int i = 0; i < n; ++i) b.setSample(c, i, value);
    return b;
}

SliceParams plain()
{
    SliceParams p;
    p.fadeInMs = 0.0f; p.fadeOutMs = 0.0f;
    return p;
}
}

struct SliceShapingTests : public juce::UnitTest
{
    SliceShapingTests() : juce::UnitTest("Slice shaping", "mu-tilate") {}

    void runTest() override
    {
        beginTest("pitch +12 semitones reads the source at twice the speed");
        {
            juce::AudioBuffer<float> src(1, 400);
            for (int i = 0; i < 400; ++i) src.setSample(0, i, (float) (i + 1) / 1000.0f);   // a ramp, so a position is readable
            SlicePlayer sp; sp.prepare(kSr);
            auto p = plain(); p.own.pitchSemi = 12.0f;
            sp.trigger(&src, { 0, 400 }, p);
            juce::AudioBuffer<float> out(1, 64); out.clear();
            sp.render(out, 64);
            for (int i = 2; i < 40; ++i)
                expectWithinAbsoluteError(out.getSample(0, i), (float) (2 * i + 1) / 1000.0f, 2.0e-4f);
        }

        beginTest("pan sends a stereo-centre slice to one side with equal power");
        {
            auto src = constant(2000, 0.5f);
            for (float pan : { -1.0f, 0.0f, 1.0f })
            {
                SlicePlayer sp; sp.prepare(kSr);
                auto p = plain(); p.own.pan = pan;
                sp.trigger(&src, { 0, 2000 }, p);
                juce::AudioBuffer<float> out(2, 128); out.clear();
                sp.render(out, 128);
                const float l = out.getSample(0, 100), r = out.getSample(1, 100);
                if (pan < 0.0f)      { expectWithinAbsoluteError(l, 0.5f * 1.41421356f, 1.0e-4f); expectWithinAbsoluteError(r, 0.0f, 1.0e-4f); }
                else if (pan > 0.0f) { expectWithinAbsoluteError(r, 0.5f * 1.41421356f, 1.0e-4f); expectWithinAbsoluteError(l, 0.0f, 1.0e-4f); }
                else                 { expectWithinAbsoluteError(l, 0.5f, 1.0e-4f); expectWithinAbsoluteError(r, 0.5f, 1.0e-4f); }
            }
        }

        beginTest("a low-pass keeps a steady level, a high-pass removes it");
        {
            auto src = constant(9600, 0.5f);
            for (int type : { 1, 2 })
            {
                SlicePlayer sp; sp.prepare(kSr);
                auto p = plain(); p.own.filterType = type; p.own.cutoffHz = 1000.0f;
                sp.trigger(&src, { 0, 9600 }, p);
                juce::AudioBuffer<float> out(1, 4800); out.clear();
                sp.render(out, 4800);
                expectWithinAbsoluteError(out.getSample(0, 4500), type == 1 ? 0.5f : 0.0f, 0.01f);
            }
        }

        beginTest("the amp attack ramps the slice in");
        {
            auto src = constant(9600, 0.5f);
            SlicePlayer sp; sp.prepare(kSr);
            auto p = plain(); p.own.ampA = 10.0f;   // 480 samples
            sp.trigger(&src, { 0, 9600 }, p);
            juce::AudioBuffer<float> out(1, 1000); out.clear();
            sp.render(out, 1000);
            expectWithinAbsoluteError(out.getSample(0, 240), 0.25f, 0.01f);
            expectWithinAbsoluteError(out.getSample(0, 800), 0.5f, 0.001f);
        }

        beginTest("a looping slice keeps playing past its end until it is cut");
        {
            juce::AudioBuffer<float> src(1, 100);
            for (int i = 0; i < 100; ++i) src.setSample(0, i, (float) (i + 1) / 1000.0f);
            SlicePlayer sp; sp.prepare(kSr);
            auto p = plain(); p.own.loop = true;
            sp.trigger(&src, { 10, 30 }, p);
            juce::AudioBuffer<float> out(1, 200); out.clear();
            sp.render(out, 200);
            expect(sp.isActive());
            // The slice is 20 samples; the last 5 cross-fade into its start and the wrap lands 5 samples in: output 20 is source 15, 25 is 20.
            expectWithinAbsoluteError(out.getSample(0, 25), src.getSample(0, 20), 1.0e-5f);
            sp.cutSlices();
            out.clear();
            sp.render(out, 200);
            expect(! sp.isActive());
        }

        beginTest("a looping slice shorter than the step keeps playing");
        {
            juce::AudioBuffer<float> src(1, 100);
            for (int i = 0; i < 100; ++i) src.setSample(0, i, 0.5f);
            SlicePlayer sp; sp.prepare(kSr);
            auto p = plain(); p.own.loop = true; p.own.pitchSemi = 24.0f;   // four source samples per output sample
            sp.trigger(&src, { 10, 13 }, p);                                // three samples long
            juce::AudioBuffer<float> out(1, 200); out.clear();
            sp.render(out, 200);
            expect(sp.isActive());
            expectGreaterThan(out.getSample(0, 150), 0.1f);
        }

        beginTest("a second pre-fade does not cut the first one off");
        {
            auto src = constant(9600, 0.5f);
            SlicePlayer sp; sp.prepare(kSr);
            auto p = plain();
            sp.triggerPre(&src, { 0, 4800 }, p);          // swells to 0.5 over 4800 samples
            juce::AudioBuffer<float> out(1, 4800); out.clear();
            juce::AudioBuffer<float> first(out.getArrayOfWritePointers(), 1, 0, 2400);
            sp.render(first, 2400);
            sp.triggerPre(&src, { 0, 4800 }, p);          // half way through, a second one starts
            juce::AudioBuffer<float> second(out.getArrayOfWritePointers(), 1, 2400, 2400);
            sp.render(second, 2400);
            // The first reaches 0.5 at its end, with the second at a quarter of its swell (0.5 * 0.5^2) on top.
            expectGreaterThan(out.getSample(0, 4790), 0.6f);
        }

        beginTest("a looping slice has no jump at its loop point");
        {
            // A 440 Hz sine, a slice that is not a whole number of cycles: a raw wrap would jump at every loop.
            const int n = 4000;
            juce::AudioBuffer<float> src(1, n);
            for (int i = 0; i < n; ++i) src.setSample(0, i, 0.5f * std::sin(2.0f * juce::MathConstants<float>::pi * 440.0f * (float) i / (float) kSr));
            SlicePlayer sp; sp.prepare(kSr);
            auto p = plain(); p.own.loop = true;
            sp.trigger(&src, { 100, 3777 }, p);               // 3677 samples, about 3.7 cycles
            juce::AudioBuffer<float> out(1, 20000); out.clear();
            for (int pos = 0; pos < 20000; pos += 500)
            {
                juce::AudioBuffer<float> view(out.getArrayOfWritePointers(), 1, pos, 500);
                sp.render(view, 500);
            }
            // The sine's own largest step is 0.5 * 2*pi*440/48000 = 0.029; a jump at the seam would be several times that.
            float worst = 0.0f;
            for (int i = 300; i < 20000; ++i) worst = std::max(worst, std::abs(out.getSample(0, i) - out.getSample(0, i - 1)));
            expectLessThan(worst, 0.06f);
            expect(sp.isActive());
        }

        beginTest("a band-pass has the same level at any resonance");
        {
            // A sine at the cutoff: the peak gain is what the scaling fixes (noise would also change with the bandwidth).
            juce::AudioBuffer<float> src(1, 48000);
            for (int i = 0; i < 48000; ++i) src.setSample(0, i, 0.3f * std::sin(2.0f * juce::MathConstants<float>::pi * 1000.0f * (float) i / (float) kSr));
            double rms[2] = {};
            for (int k = 0; k < 2; ++k)
            {
                SlicePlayer sp; sp.prepare(kSr);
                auto p = plain(); p.own.filterType = 3; p.own.cutoffHz = 1000.0f; p.own.resonance = k == 0 ? 0.0f : 1.0f;
                sp.trigger(&src, { 0, 48000 }, p);
                juce::AudioBuffer<float> out(1, 24000); out.clear();
                sp.render(out, 24000);
                rms[k] = out.getRMSLevel(0, 4000, 20000);
            }
            expectLessThan(std::abs(20.0 * std::log10(rms[1] / rms[0])), 2.0);   // before the scaling these differed by about 34 dB
        }

        beginTest("a stretched slice plays at 192 kHz (the stretcher's work is split)");
        {
            const double sr = 192000.0;
            const int n = 96000;
            juce::AudioBuffer<float> src(1, n);
            for (int i = 0; i < n; ++i) src.setSample(0, i, 0.5f * std::sin(2.0f * juce::MathConstants<float>::pi * 440.0f * (float) i / (float) sr));
            SlicePlayer sp; sp.prepare(sr, 512);
            auto p = plain(); p.timeRatio = 1.25f;
            sp.trigger(&src, { 0, n }, p);
            juce::AudioBuffer<float> out(1, 512);
            int total = 0; float peak = 0.0f;
            while (sp.isActive() && total < 3 * n)
            {
                out.clear();
                sp.render(out, 512);
                peak = std::max(peak, out.getMagnitude(0, 512));
                total += 512;
            }
            expectGreaterThan(peak, 0.3f);
            expectWithinAbsoluteError((double) total, 1.25 * n, 0.05 * n);
        }

        beginTest("a pre-fade is a voice of its own: a slice trigger and a slice cut leave it playing");
        {
            auto src = constant(9600, 0.5f);
            SlicePlayer sp; sp.prepare(kSr);
            auto p = plain();
            sp.triggerPre(&src, { 0, 4800 }, p);          // 100 ms
            sp.trigger(&src, { 5000, 6000 }, p);
            sp.cutSlices();
            juce::AudioBuffer<float> out(1, 4800); out.clear();
            sp.render(out, 4800);
            expectWithinAbsoluteError(out.getSample(0, 4790), 0.5f, 0.01f);   // the swell reaches its full level at the end
            expect(std::abs(out.getSample(0, 1000)) < 0.05f);                 // and is still quiet near the start
        }
    }
};

static SliceShapingTests sliceShapingTests;
