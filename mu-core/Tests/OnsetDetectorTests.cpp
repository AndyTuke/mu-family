// Onset detection in mu-core (Analysis/OnsetDetector): synthetic loops with known attack times. Attacks are found within
// 4 ms and never after the hit, at the loop's edges too; Sensitivity and Min gap behave; silence and a quiet hiss have no
// onsets; soft hits between loud ones need a high Sensitivity; one-channel hits are found (the mono mix); a band that
// excludes a hit's frequencies hides it; a loop shorter than a frame has an empty curve.

#include "Analysis/OnsetDetector.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace
{
constexpr double kSr = 48000.0;

// A loop of decaying noise bursts at the given times (seconds): each starts at full level and falls over about 80 ms.
juce::AudioBuffer<float> burstLoop(double seconds, const std::vector<double>& times, float level = 0.8f)
{
    const int n = (int) (seconds * kSr);
    juce::AudioBuffer<float> b(1, n);
    b.clear();
    juce::Random rng(1234);
    for (double t : times)
    {
        const int s0 = (int) (t * kSr);
        for (int i = 0; s0 + i < n && i < (int) (0.4 * kSr); ++i)
            b.getWritePointer(0)[s0 + i] += level * std::exp(-(float) i / (0.02f * (float) kSr)) * (rng.nextFloat() * 2.0f - 1.0f);
    }
    return b;
}

// Index of the onset nearest `t` seconds, and its distance in ms.
double nearestMs(const std::vector<int>& onsets, double t)
{
    double best = 1.0e9;
    for (int o : onsets) best = std::min(best, std::abs((double) o / kSr - t) * 1000.0);
    return best;
}
}

struct OnsetDetectorTests : public juce::UnitTest
{
    OnsetDetectorTests() : juce::UnitTest("Onset detector", "mu-core") {}

    void runTest() override
    {
        const std::vector<double> hits { 0.0, 0.5, 1.0, 1.5 };

        beginTest("each attack is found within 4 ms (spectral flux)");
        {
            auto loop = burstLoop(2.0, hits);
            auto curve = mu_core::computeCurve(loop, kSr, mu_core::OnsetMethod::SpectralFlux);
            auto onsets = mu_core::pickOnsets(curve, {}, &loop);
            for (double t : { 0.5, 1.0, 1.5 })
                expectLessThan(nearestMs(onsets, t), 4.0);
            expectLessOrEqual((int) onsets.size(), 5);   // no flood of false onsets
        }

        beginTest("SuperFlux finds the same attacks");
        {
            auto loop = burstLoop(2.0, hits);
            auto curve = mu_core::computeCurve(loop, kSr, mu_core::OnsetMethod::SuperFlux);
            auto onsets = mu_core::pickOnsets(curve, {}, &loop);
            for (double t : { 0.5, 1.0, 1.5 })
                expectLessThan(nearestMs(onsets, t), 4.0);
        }

        beginTest("silence has no onsets");
        {
            juce::AudioBuffer<float> silent(1, (int) (2.0 * kSr));
            silent.clear();
            auto curve = mu_core::computeCurve(silent, kSr, mu_core::OnsetMethod::SpectralFlux);
            expectEquals((int) mu_core::pickOnsets(curve, {}, &silent).size(), 0);
        }

        beginTest("higher sensitivity never picks fewer onsets");
        {
            auto loop = burstLoop(2.0, { 0.0, 0.4, 0.8, 1.2, 1.6 });
            // A quieter burst between the loud ones is picked only at high sensitivity.
            auto quiet = burstLoop(2.0, { 0.2, 0.6, 1.0, 1.4 }, 0.12f);
            for (int c = 0; c < loop.getNumSamples(); ++c) loop.getWritePointer(0)[c] += quiet.getSample(0, c);
            auto curve = mu_core::computeCurve(loop, kSr, mu_core::OnsetMethod::SpectralFlux);
            mu_core::OnsetPickSettings lo, hi;
            lo.sensitivity = 0.1f; hi.sensitivity = 0.9f;
            expectGreaterThan((int) mu_core::pickOnsets(curve, hi).size(), (int) mu_core::pickOnsets(curve, lo).size());
        }

        beginTest("min gap removes the weaker of two close onsets");
        {
            auto loop = burstLoop(2.0, { 0.5, 0.53, 1.2 });
            auto curve = mu_core::computeCurve(loop, kSr, mu_core::OnsetMethod::SpectralFlux);
            mu_core::OnsetPickSettings tight, wide;
            tight.minGapMs = 5.0f; wide.minGapMs = 200.0f;
            wide.sensitivity = tight.sensitivity = 0.9f;
            auto w = mu_core::pickOnsets(curve, wide);
            for (size_t i = 1; i < w.size(); ++i)
                expectGreaterOrEqual((double) (w[i] - w[i - 1]) / kSr * 1000.0, 190.0);
            expectGreaterOrEqual((int) mu_core::pickOnsets(curve, tight).size(), (int) w.size());
        }

        beginTest("the position is at the attack, never after it, and within the pre-roll and snap of it");
        {
            auto loop = burstLoop(2.0, hits);
            auto curve = mu_core::computeCurve(loop, kSr, mu_core::OnsetMethod::SpectralFlux);
            const auto onsets = mu_core::pickOnsets(curve, {}, &loop);
            for (double t : { 0.5, 1.0, 1.5 })
            {
                double nearest = 1.0e9;
                for (int o : onsets) if (std::abs((double) o / kSr - t) < std::abs(nearest / 1000.0)) nearest = ((double) o / kSr - t) * 1000.0;
                expectLessOrEqual(nearest, 0.5);     // not after the attack
                expectGreaterOrEqual(nearest, -3.5); // and not more than the pre-roll (2 ms) and snap (1 ms) before it
            }
        }

        beginTest("an attack a few hundred samples into the loop and one just before its end are found");
        {
            auto loop = burstLoop(2.0, { 0.0075, 1.0, 1.99 });
            auto curve = mu_core::computeCurve(loop, kSr, mu_core::OnsetMethod::SpectralFlux);
            const auto onsets = mu_core::pickOnsets(curve, {}, &loop);
            expectLessThan(nearestMs(onsets, 0.0075), 4.0);
            expectLessThan(nearestMs(onsets, 1.0), 4.0);
            expectLessThan(nearestMs(onsets, 1.99), 4.0);
        }

        beginTest("a quiet hiss has no onsets, even though the curve is scaled to its own peak");
        {
            juce::AudioBuffer<float> hiss(1, (int) (2.0 * kSr));
            juce::Random rng(99);
            for (int i = 0; i < hiss.getNumSamples(); ++i) hiss.setSample(0, i, 0.0002f * (rng.nextFloat() * 2.0f - 1.0f));
            auto curve = mu_core::computeCurve(hiss, kSr, mu_core::OnsetMethod::SpectralFlux);
            mu_core::OnsetPickSettings high; high.sensitivity = 1.0f;
            expectEquals((int) mu_core::pickOnsets(curve, high, &hiss).size(), 0);
        }

        beginTest("soft hits between loud ones are found at high sensitivity and not at low");
        {
            auto loop = burstLoop(2.0, { 0.0, 0.4, 0.8, 1.2, 1.6 });
            auto quiet = burstLoop(2.0, { 0.2, 0.6, 1.0, 1.4 }, 0.12f);
            for (int i = 0; i < loop.getNumSamples(); ++i) loop.getWritePointer(0)[i] += quiet.getSample(0, i);
            auto curve = mu_core::computeCurve(loop, kSr, mu_core::OnsetMethod::SpectralFlux);
            mu_core::OnsetPickSettings lo, hi;
            lo.sensitivity = 0.1f; hi.sensitivity = 0.9f;
            const auto few = mu_core::pickOnsets(curve, lo, &loop), many = mu_core::pickOnsets(curve, hi, &loop);
            expectGreaterThan((int) many.size(), (int) few.size());
            for (double t : { 0.2, 0.6, 1.0, 1.4 }) expectLessThan(nearestMs(many, t), 4.0);
        }

        beginTest("detection uses the mono mix: hits in only one channel are found");
        {
            auto mono = burstLoop(2.0, hits);
            juce::AudioBuffer<float> stereo(2, mono.getNumSamples());
            stereo.clear();
            stereo.copyFrom(1, 0, mono, 0, 0, mono.getNumSamples());   // left silent
            auto curve = mu_core::computeCurve(stereo, kSr, mu_core::OnsetMethod::SpectralFlux);
            const auto onsets = mu_core::pickOnsets(curve, {}, &stereo);
            for (double t : { 0.5, 1.0, 1.5 }) expectLessThan(nearestMs(onsets, t), 4.0);
        }

        beginTest("a band that excludes a hit's frequencies hides it");
        {
            // A 100 Hz thump at 0.5 s, and a 6 kHz tick at 1.0 s: a 2 kHz low cut keeps only the tick.
            juce::AudioBuffer<float> loop(1, (int) (2.0 * kSr));
            loop.clear();
            for (int i = 0; i < (int) (0.15 * kSr); ++i)
            {
                const float env = std::exp(-(float) i / (0.03f * (float) kSr));
                loop.getWritePointer(0)[(int) (0.5 * kSr) + i] += 0.8f * env * std::sin(2.0f * juce::MathConstants<float>::pi * 100.0f * (float) i / (float) kSr);
                loop.getWritePointer(0)[(int) (1.0 * kSr) + i] += 0.8f * env * std::sin(2.0f * juce::MathConstants<float>::pi * 6000.0f * (float) i / (float) kSr);
            }
            auto all = mu_core::pickOnsets(mu_core::computeCurve(loop, kSr, mu_core::OnsetMethod::SpectralFlux), {}, &loop);
            auto high = mu_core::pickOnsets(mu_core::computeCurve(loop, kSr, mu_core::OnsetMethod::SpectralFlux, 2000.0f), {}, &loop);
            expectLessThan(nearestMs(all, 0.5), 5.0);
            expectLessThan(nearestMs(all, 1.0), 5.0);
            expectLessThan(nearestMs(high, 1.0), 5.0);
            expectGreaterThan(nearestMs(high, 0.5), 50.0);
        }

        beginTest("a loop shorter than a frame has an empty curve");
        {
            juce::AudioBuffer<float> tiny(1, 100);
            tiny.clear();
            expect(mu_core::computeCurve(tiny, kSr, mu_core::OnsetMethod::SpectralFlux).empty());
        }
    }
};

static OnsetDetectorTests onsetDetectorTests;
