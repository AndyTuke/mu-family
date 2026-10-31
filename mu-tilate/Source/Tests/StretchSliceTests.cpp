// The stretched slice (SlicePlayer with a timeRatio): the output has the stretched length, starts as the source
// itself (the dry head), keeps the pitch and level through the stretch, never plays the next slice's audio in its
// tail, and allocates nothing on the audio thread.

#define MU_ALLOCATION_PROBE_DEFINE_OPERATORS
#include "AllocationProbe.h"   // mu-core/Tests: counts the audio-thread heap use (this exe's operators live here)

#include "Audio/SlicePlayer.h"

#include <juce_core/juce_core.h>

#include <cmath>

using namespace mu_tilate;

namespace
{
constexpr double kSr = 48000.0;

juce::AudioBuffer<float> sine(int n, float freq, float amp = 0.5f)
{
    juce::AudioBuffer<float> b(1, n);
    for (int i = 0; i < n; ++i) b.setSample(0, i, amp * std::sin(2.0f * juce::MathConstants<float>::pi * freq * (float) i / (float) kSr));
    return b;
}

// Renders the player in blocks until it goes quiet or `maxSamples`, returns the output.
juce::AudioBuffer<float> renderAll(SlicePlayer& p, int maxSamples, int block = 128)
{
    juce::AudioBuffer<float> out(1, maxSamples);
    out.clear();
    for (int pos = 0; pos < maxSamples; pos += block)
    {
        juce::AudioBuffer<float> view(out.getArrayOfWritePointers(), 1, pos, std::min(block, maxSamples - pos));
        p.render(view, view.getNumSamples());
    }
    return out;
}

double rms(const juce::AudioBuffer<float>& b, int from, int count)
{
    double s = 0.0;
    for (int i = from; i < from + count; ++i) s += (double) b.getSample(0, i) * b.getSample(0, i);
    return std::sqrt(s / (double) count);
}

int zeroCrossings(const juce::AudioBuffer<float>& b, int from, int count)
{
    int c = 0;
    for (int i = from + 1; i < from + count; ++i)
        if ((b.getSample(0, i - 1) < 0.0f) != (b.getSample(0, i) < 0.0f)) ++c;
    return c;
}

SliceParams stretched(float ratio)
{
    SliceParams p;
    p.timeRatio = ratio;
    p.fadeInMs = 0.0f;
    return p;
}
}

struct StretchSliceTests : public juce::UnitTest
{
    StretchSliceTests() : juce::UnitTest("Stretched slice", "mu-tilate") {}

    void runTest() override
    {
        beginTest("a 1.5x stretch lasts 1.5 times the slice, then stops");
        {
            auto src = sine(30000, 440.0f);
            SlicePlayer p; p.prepare(kSr, 128);
            p.trigger(&src, { 0, 24000 }, stretched(1.5f));
            auto out = renderAll(p, 40000);
            expectGreaterThan(rms(out, 34000, 1000), 0.05);      // still sounding near the end of 36000
            expectEquals(rms(out, 36100, 3000), 0.0);            // and silent after it
            expect(! p.isActive());
        }

        beginTest("a compression (ratio 0.8) ends early");
        {
            auto src = sine(30000, 440.0f);
            SlicePlayer p; p.prepare(kSr, 128);
            p.trigger(&src, { 0, 24000 }, stretched(0.8f));
            auto out = renderAll(p, 30000);
            expectGreaterThan(rms(out, 18000, 1000), 0.05);      // total 19200
            expectEquals(rms(out, 19300, 3000), 0.0);
        }

        beginTest("the dry head is the source itself for the hold");
        {
            auto src = sine(30000, 440.0f);
            SlicePlayer p; p.prepare(kSr, 128);
            auto sp = stretched(1.5f); sp.holdMs = 10.0f; sp.strength = 1.0f;
            p.trigger(&src, { 0, 24000 }, sp);
            auto out = renderAll(p, 2000);
            for (int i = 0; i < 480; ++i)
                expectWithinAbsoluteError(out.getSample(0, i), src.getSample(0, i), 1.0e-5f);
        }

        beginTest("the stretch keeps the pitch and the level");
        {
            auto src = sine(48000, 440.0f);
            SlicePlayer p; p.prepare(kSr, 128);
            p.trigger(&src, { 0, 24000 }, stretched(1.5f));
            auto out = renderAll(p, 36000);
            // 8000 samples in the steady part: 440 Hz gives about 146.7 zero crossings.
            const int zc = zeroCrossings(out, 20000, 8000);
            expectGreaterOrEqual(zc, 142);
            expectLessOrEqual(zc, 152);
            const double ratio = rms(out, 20000, 8000) / rms(src, 0, 8000);
            expectGreaterThan(ratio, 0.8);
            expectLessThan(ratio, 1.25);
        }

        beginTest("the neighbouring slice never leaks into the tail");
        {
            // The slice is a quiet sine; after it the loop holds a loud tone, which the stretcher must not read.
            juce::AudioBuffer<float> src(1, 48000);
            for (int i = 0; i < 48000; ++i)
                src.setSample(0, i, (i < 20000 ? 0.2f : 0.9f) * std::sin(2.0f * juce::MathConstants<float>::pi * 440.0f * (float) i / (float) kSr));
            SlicePlayer p; p.prepare(kSr, 128);
            p.trigger(&src, { 0, 20000 }, stretched(1.3f));
            auto out = renderAll(p, 30000);
            expectLessThan(out.getMagnitude(0, 0, 30000), 0.3f);
        }

        beginTest("Strength 0 has no dry head: the stretch is already sounding in the first 10 ms");
        {
            auto src = sine(30000, 440.0f);
            SlicePlayer p; p.prepare(kSr, 128);
            auto sp = stretched(1.5f); sp.strength = 0.0f;
            p.trigger(&src, { 0, 24000 }, sp);
            auto out = renderAll(p, 2000);
            expectGreaterThan(rms(out, 240, 240), 0.1);
        }

        beginTest("a new slice cuts a stretched one through the cut fade");
        {
            auto src = sine(60000, 440.0f);
            SlicePlayer p; p.prepare(kSr, 128);
            p.trigger(&src, { 0, 40000 }, stretched(1.5f));
            auto first = renderAll(p, 4000);
            p.trigger(nullptr, { 0, 0 }, SliceParams {});     // a gap: only the cut
            auto tail = renderAll(p, 1000);
            expectEquals(rms(tail, 500, 400), 0.0);
            expect(! p.isActive());
            expectGreaterThan(rms(first, 3000, 500), 0.05);
        }

        beginTest("trigger and render allocate nothing on the audio thread");
        {
            auto src = sine(60000, 440.0f);
            SlicePlayer p; p.prepare(kSr, 128);
            juce::AudioBuffer<float> out(1, 128);
            mu_alloc::Armed region;
            for (int k = 0; k < 3; ++k)                          // three stretched slices, the second two cutting the one before
            {
                p.trigger(&src, { k * 10000, k * 10000 + 9000 }, stretched(1.3f + 0.2f * (float) k));
                for (int b = 0; b < 20; ++b) { out.clear(); p.render(out, 128); }
            }
            region.stop();
            expectEquals(region.allocs(), 0, "the stretch path touched the heap");
        }

        beginTest("the allocation probe sees a heap allocation (so the zero above means something)");
        {
            mu_alloc::Armed region;
            auto* leak = new float[64];
            region.stop();
            delete[] leak;
            expectGreaterThan(region.allocs(), 0);
        }
    }
};

static StretchSliceTests stretchSliceTests;
