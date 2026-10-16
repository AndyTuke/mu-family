// The reverb Decay knob: the per-algorithm presets are RT60 seconds, Decay sets the tail length, and Size
// does not. The slot is driven directly (wet-only processReturn, 48 kHz, blocks of 512) with a fixed-seed
// noise burst; the tail level is measured in two windows and the drop between them gives the decay rate.
//
// The expected drops are reasoned from the library (rt20 = decay / 3 and getDecayDb = -20 / rt20, so
// 60 / decay dB per second), not measured; every threshold is generous and says so where it is used.

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>

#include "Audio/FX/Slots/ReverbSlot.h"

namespace
{
    constexpr double kRate  = 48000.0;
    constexpr int    kBlock = 512;

    // RMS (linear) of the left channel over [startSample, endSample) of a rendered tail.
    double windowRms(const juce::AudioBuffer<float>& b, int startSample, int endSample)
    {
        const float* d = b.getReadPointer(0);
        double sum = 0.0;
        for (int i = startSample; i < endSample; ++i) sum += (double) d[i] * d[i];
        return std::sqrt(sum / (double) (endSample - startSample));
    }

    // Renders a Hall reverb (enabled, level 1) fed a 50 ms noise burst, then silence, and returns the level drop
    // in dB between a 100 ms window starting at 0.5 s and one starting at 1.5 s after the burst.
    double tailDropDb(float decaySeconds, float size)
    {
        ReverbSlot slot;
        slot.setAlgorithm(1);          // Hall
        slot.setEnabled(true);
        slot.setLevel(1.0f);
        slot.setParam("size", size);
        slot.setParam("decay", decaySeconds);
        slot.prepare(kRate, kBlock);   // prepare re-sends the held settings to the reverb

        const int burst    = (int) (0.05 * kRate);
        const int total    = ((int) (1.7 * kRate) / kBlock + 1) * kBlock;
        juce::AudioBuffer<float> out(1, total);
        juce::AudioBuffer<float> blk(2, kBlock);
        juce::Random rng(20261016);

        // Block loop: noise for the first 50 ms, silence after; collect the left channel of the wet return.
        for (int pos = 0; pos < total; pos += kBlock)
        {
            blk.clear();
            for (int i = 0; i < kBlock; ++i)
                if (pos + i < burst)
                {
                    blk.setSample(0, i, 0.5f * (rng.nextFloat() * 2.0f - 1.0f));
                    blk.setSample(1, i, 0.5f * (rng.nextFloat() * 2.0f - 1.0f));
                }
            slot.processReturn(blk);
            out.copyFrom(0, pos, blk, 0, 0, kBlock);
        }

        const int w = (int) (0.1 * kRate);
        const int a = burst + (int) (0.5 * kRate);
        const int b = burst + (int) (1.5 * kRate);
        const double rmsA = windowRms(out, a, a + w);
        const double rmsB = windowRms(out, b, b + w);
        jassert(rmsA > 0.0 && rmsB > 0.0);
        return 20.0 * std::log10(rmsA / juce::jmax(rmsB, 1.0e-12));
    }
}

struct ReverbDecayTests : juce::UnitTest
{
    ReverbDecayTests() : juce::UnitTest("Reverb Decay knob: presets, tail length, Size independence", "mu-core") {}

    void runTest() override
    {
        beginTest("each algorithm preset sets Decay (RT60 seconds) and leaves the other knobs at their preset");
        {
            struct Preset { float size, decay, pre, diff, damp, mod; };
            // Room, Hall, Plate, Spring.
            const Preset presets[4] = {
                { 0.25f, 0.60f,  5.0f, 0.6f, 0.6f, 0.10f },
                { 0.75f, 2.50f, 25.0f, 0.8f, 0.3f, 0.15f },
                { 0.45f, 1.80f, 10.0f, 0.9f, 0.5f, 0.30f },
                { 0.15f, 0.35f,  2.0f, 0.3f, 0.8f, 0.60f } };
            ReverbSlot slot;
            for (int algo = 0; algo < 4; ++algo)
            {
                slot.setAlgorithm(algo);
                const auto& e = presets[algo];
                const juce::String name = "algorithm " + juce::String(algo);
                expectWithinAbsoluteError(slot.getDecay(),     e.decay, 1.0e-6f, name + " decay");
                expectWithinAbsoluteError(slot.getSize(),      e.size,  1.0e-6f, name + " size");
                expectWithinAbsoluteError(slot.getPreDelay(),  e.pre,   1.0e-6f, name + " pre-delay");
                expectWithinAbsoluteError(slot.getDiffusion(), e.diff,  1.0e-6f, name + " diffusion");
                expectWithinAbsoluteError(slot.getDamp(),      e.damp,  1.0e-6f, name + " damp");
                expectWithinAbsoluteError(slot.getMod(),       e.mod,   1.0e-6f, name + " mod");
            }
        }

        beginTest("setParam(\"decay\") stores the value, clamped to 0.05..20 s");
        {
            ReverbSlot slot;
            slot.setParam("decay", 5.0f);
            expectWithinAbsoluteError(slot.getDecay(), 5.0f, 1.0e-6f, "5 s");
            slot.setParam("decay", 100.0f);
            expectWithinAbsoluteError(slot.getDecay(), 20.0f, 1.0e-6f, "upper clamp");
            slot.setParam("decay", -1.0f);
            expectWithinAbsoluteError(slot.getDecay(), 0.05f, 1.0e-6f, "lower clamp");
        }

        beginTest("Decay sets the tail length: the level falls about 60 dB per Decay second");
        {
            const double drop1 = tailDropDb(1.0f, 0.75f);
            const double drop4 = tailDropDb(4.0f, 0.75f);
            logMessage("drop 0.5 s to 1.5 s: decay 1 s " + juce::String(drop1, 1) + " dB, decay 4 s " + juce::String(drop4, 1) + " dB");
            // Reasoned: 60 dB (decay 1 s) and 15 dB (decay 4 s) over the 1 s between the windows; damping and the
            // noise-window RMS move the figures, so the bands are wide.
            expect(drop1 > 30.0 && drop1 < 90.0, "decay 1 s drops 30 to 90 dB (got " + juce::String(drop1, 1) + ")");
            expect(drop4 > 6.0 && drop4 < 30.0,  "decay 4 s drops 6 to 30 dB (got " + juce::String(drop4, 1) + ")");
            expect(drop1 > 2.5 * drop4, "the short decay falls much faster than the long one");
        }

        beginTest("Size does not change the decay rate");
        {
            const double small = tailDropDb(2.0f, 0.2f);
            const double large = tailDropDb(2.0f, 0.9f);
            logMessage("drop at decay 2 s: size 0.2 " + juce::String(small, 1) + " dB, size 0.9 " + juce::String(large, 1) + " dB");
            // Reasoned: 30 dB for both. The 6 dB agreement allows the different echo density and window noise.
            expect(std::abs(small - large) < 6.0,
                   "size 0.2 and 0.9 drop within 6 dB of each other (" + juce::String(small, 1) + " vs " + juce::String(large, 1) + ")");
        }
    }
};

static ReverbDecayTests reverbDecayTests;
