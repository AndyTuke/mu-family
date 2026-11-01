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
            expectWithinAbsoluteError(out.getSample(0, 25), src.getSample(0, 15), 1.0e-5f);   // wrapped: 25 mod 20 = 5
            sp.cutSlices();
            out.clear();
            sp.render(out, 200);
            expect(! sp.isActive());
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
