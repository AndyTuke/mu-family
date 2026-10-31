// The slice player: native playback is the source exactly, a new slice cuts the old one through the declick
// fade (no step in the output), rate and reverse move the read position, and the slice's neighbours never leak in.

#include "Audio/SlicePlayer.h"

#include <juce_core/juce_core.h>

using namespace mu_tilate;

namespace
{
constexpr double kSr = 48000.0;

// A source whose sample i holds (i + 1) / 1000, so a position is readable from the value.
juce::AudioBuffer<float> ramp(int n, int channels = 1)
{
    juce::AudioBuffer<float> b(channels, n);
    for (int c = 0; c < channels; ++c)
        for (int i = 0; i < n; ++i) b.setSample(c, i, (float) (i + 1) / 1000.0f);
    return b;
}

SliceParams noFades()
{
    SliceParams p;
    p.fadeInMs = 0.0f; p.fadeOutMs = 0.0f;
    return p;
}
}

struct SlicePlayerTests : public juce::UnitTest
{
    SlicePlayerTests() : juce::UnitTest("Slice player", "mu-tilate") {}

    void runTest() override
    {
        beginTest("native rate with no fades plays the slice exactly, then stops");
        {
            auto src = ramp(100);
            SlicePlayer p; p.prepare(kSr);
            p.trigger(&src, { 10, 30 }, noFades());
            juce::AudioBuffer<float> out(1, 64); out.clear();
            p.render(out, 64);
            // The last slice sample is attenuated by the one-sample fade-out minimum only when fades are on; here it is exact.
            for (int i = 0; i < 20; ++i)
                expectWithinAbsoluteError(out.getSample(0, i), src.getSample(0, 10 + i), 1.0e-6f);
            for (int i = 20; i < 64; ++i) expectEquals(out.getSample(0, i), 0.0f);
            expect(! p.isActive());
        }

        beginTest("a mono source plays in both output channels");
        {
            auto src = ramp(50);
            SlicePlayer p; p.prepare(kSr);
            p.trigger(&src, { 0, 40 }, noFades());
            juce::AudioBuffer<float> out(2, 16); out.clear();
            p.render(out, 16);
            for (int i = 0; i < 16; ++i) expectEquals(out.getSample(0, i), out.getSample(1, i));
        }

        beginTest("the slice's neighbours never leak into the output");
        {
            auto src = ramp(100);
            SlicePlayer p; p.prepare(kSr);
            p.trigger(&src, { 40, 60 }, noFades());
            juce::AudioBuffer<float> out(1, 64); out.clear();
            p.render(out, 64);
            float lo = 1.0e9f, hi = -1.0e9f;
            for (int i = 0; i < 20; ++i) { lo = std::min(lo, out.getSample(0, i)); hi = std::max(hi, out.getSample(0, i)); }
            expectGreaterOrEqual(lo, 0.041f - 1.0e-6f);
            expectLessOrEqual(hi, 0.060f + 1.0e-6f);
        }

        beginTest("rate 2 reads every other sample, so the slice takes half as long");
        {
            auto src = ramp(100);
            SlicePlayer p; p.prepare(kSr);
            auto sp = noFades(); sp.rate = 2.0f;
            p.trigger(&src, { 0, 40 }, sp);
            juce::AudioBuffer<float> out(1, 40); out.clear();
            p.render(out, 40);
            for (int i = 0; i < 18; ++i)
                expectWithinAbsoluteError(out.getSample(0, i), src.getSample(0, 2 * i), 1.0e-5f);
            expectEquals(out.getSample(0, 30), 0.0f);
        }

        beginTest("reverse plays the slice backwards");
        {
            auto src = ramp(100);
            SlicePlayer p; p.prepare(kSr);
            auto sp = noFades(); sp.reverse = true;
            p.trigger(&src, { 10, 30 }, sp);
            juce::AudioBuffer<float> out(1, 24); out.clear();
            p.render(out, 24);
            for (int i = 0; i < 20; ++i)
                expectWithinAbsoluteError(out.getSample(0, i), src.getSample(0, 29 - i), 1.0e-6f);
        }

        beginTest("fades: the first and last samples of a slice are attenuated, the middle is untouched");
        {
            juce::AudioBuffer<float> src(1, 4800);
            for (int i = 0; i < 4800; ++i) src.setSample(0, i, 1.0f);
            SlicePlayer p; p.prepare(kSr);
            p.trigger(&src, { 0, 4800 }, SliceParams {});
            juce::AudioBuffer<float> out(1, 4800); out.clear();
            p.render(out, 4800);
            expectLessThan(out.getSample(0, 0), 0.1f);
            expectWithinAbsoluteError(out.getSample(0, 2400), 1.0f, 1.0e-6f);
            expectLessThan(out.getSample(0, 4799), 0.1f);
        }

        beginTest("a new slice cuts the sounding one through the cut fade: no step larger than the fade allows");
        {
            juce::AudioBuffer<float> src(1, 48000);
            for (int i = 0; i < 48000; ++i) src.setSample(0, i, 1.0f);   // a constant, so any step is the fade
            SlicePlayer p; p.prepare(kSr);
            p.trigger(&src, { 0, 40000 }, noFades());
            juce::AudioBuffer<float> out(1, 1000); out.clear();
            p.render(out, 500);
            // The second slice is silent (empty range): the first must fall to 0 over kCutFadeMs, not at once.
            p.trigger(&src, { 0, 0 }, noFades());
            juce::AudioBuffer<float> tail(1, 500); tail.clear();
            p.render(tail, 500);
            float maxStep = std::abs(tail.getSample(0, 0) - 1.0f);
            for (int i = 1; i < 500; ++i) maxStep = std::max(maxStep, std::abs(tail.getSample(0, i) - tail.getSample(0, i - 1)));
            expectLessThan(maxStep, 2.0f / (float) (SlicePlayer::kCutFadeMs * 0.001 * kSr));
            expectEquals(tail.getSample(0, 499), 0.0f);
            expect(! p.isActive());
        }

        beginTest("a slice past the end of the loop is clamped; an empty one only cuts");
        {
            auto src = ramp(100);
            SlicePlayer p; p.prepare(kSr);
            p.trigger(&src, { 90, 500 }, noFades());
            juce::AudioBuffer<float> out(1, 32); out.clear();
            p.render(out, 32);
            expectWithinAbsoluteError(out.getSample(0, 9), src.getSample(0, 99), 1.0e-6f);
            expectEquals(out.getSample(0, 10), 0.0f);
            p.trigger(nullptr, { 0, 10 }, noFades());
            expect(! p.isActive());
        }
    }
};

static SlicePlayerTests slicePlayerTests;
