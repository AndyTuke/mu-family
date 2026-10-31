// Onset detection on synthetic loops with known attack times, and the slice list built from it: detected markers
// land within a few ms of the attacks, Sensitivity and Min gap behave, silence has no onsets, and the list covers
// the loop and keeps pinned markers through a re-detect.

#include "Analysis/OnsetDetector.h"
#include "Sequencer/SliceList.h"

#include <juce_core/juce_core.h>

#include <cmath>

using namespace mu_tilate;

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

struct SliceDetectionTests : public juce::UnitTest
{
    SliceDetectionTests() : juce::UnitTest("Slice detection", "mu-tilate") {}

    void runTest() override
    {
        const std::vector<double> hits { 0.0, 0.5, 1.0, 1.5 };

        beginTest("each attack is found within 12 ms (spectral flux)");
        {
            auto loop = burstLoop(2.0, hits);
            auto curve = mu_core::computeCurve(loop, kSr, mu_core::OnsetMethod::SpectralFlux);
            auto onsets = mu_core::pickOnsets(curve, {}, &loop);
            for (double t : { 0.5, 1.0, 1.5 })
                expectLessThan(nearestMs(onsets, t), 12.0);
            expectLessOrEqual((int) onsets.size(), 5);   // no flood of false onsets
        }

        beginTest("SuperFlux finds the same attacks");
        {
            auto loop = burstLoop(2.0, hits);
            auto curve = mu_core::computeCurve(loop, kSr, mu_core::OnsetMethod::SuperFlux);
            auto onsets = mu_core::pickOnsets(curve, {}, &loop);
            for (double t : { 0.5, 1.0, 1.5 })
                expectLessThan(nearestMs(onsets, t), 12.0);
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
            expectGreaterOrEqual((int) mu_core::pickOnsets(curve, hi).size(), (int) mu_core::pickOnsets(curve, lo).size());
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

        beginTest("a loop shorter than a frame has an empty curve");
        {
            juce::AudioBuffer<float> tiny(1, 100);
            tiny.clear();
            expect(mu_core::computeCurve(tiny, kSr, mu_core::OnsetMethod::SpectralFlux).empty());
        }

        beginTest("the slice list covers the loop from 0 to its end");
        {
            SliceList list(48000);
            list.applyDetected({ 12000, 24000, 36000 });
            expectEquals(list.size(), 4);
            expectEquals(list.slice(0).start, 0);
            expectEquals(list.slice(3).end, 48000);
            for (int i = 0; i + 1 < list.size(); ++i) expectEquals(list.slice(i).end, list.slice(i + 1).start);
            expectEquals(list.indexAt(0), 0);
            expectEquals(list.indexAt(23999), 1);
            expectEquals(list.indexAt(24000), 2);
            expectEquals(list.indexAt(48000), -1);
        }

        beginTest("a pinned marker survives a re-detect, an unpinned one does not");
        {
            SliceList list(48000);
            list.applyDetected({ 10000, 30000 });
            list.addMarker(20000);                      // hand-placed, pinned
            list.applyDetected({ 15000 });
            expectEquals(list.size(), 3);               // 0, 15000 (new), 20000 (pinned)
            expectEquals(list.marker(1).position, 15000);
            expectEquals(list.marker(2).position, 20000);
            expect(list.marker(2).pinned);
        }

        beginTest("markers are refused when too close, at 0, or past the end; moves stay between neighbours");
        {
            SliceList list(48000);
            list.applyDetected({ 12000, 24000 });
            expectEquals(list.addMarker(12010), -1);
            expectEquals(list.addMarker(0), -1);
            expectEquals(list.addMarker(48000), -1);
            expect(! list.removeMarker(0));
            expectEquals(list.moveMarker(1, 30000), 1);
            expectEquals(list.marker(1).position, 24000 - 32);
            expect(list.removeMarker(1));
            expectEquals(list.size(), 2);
        }

        beginTest("even division makes equal slices and shortening the loop drops markers beyond it");
        {
            SliceList list(48000);
            list.divideEvenly(8);
            expectEquals(list.size(), 8);
            for (int i = 0; i < 8; ++i) expectEquals(list.slice(i).length(), 6000);
            list.setLoopLength(24000);
            expectEquals(list.size(), 4);
            expectEquals(list.slice(3).end, 24000);
        }
    }
};

static SliceDetectionTests sliceDetectionTests;
