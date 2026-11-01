// The shared per-voice DSP helpers (Audio/EqualPowerPan.h, TptSvf.h, AgeEnvelope.h): the pan law keeps the power constant, the
// state-variable filter passes, blocks and stays finite where it should, the band-pass peak does not move with the resonance,
// and the envelope is the attack, the decay and then the sustain.

#include "Audio/AgeEnvelope.h"
#include "Audio/EqualPowerPan.h"
#include "Audio/TptSvf.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace
{
constexpr double kSr = 48000.0;

// RMS of the filter's output for a sine at `hz`, measured after the filter has settled.
double sineRms(mu_core::TptSvf& f, double hz, int channel = 0)
{
    double sum = 0.0;
    const int n = 24000;
    for (int i = 0; i < n; ++i)
    {
        const float y = f.process(0.5f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * hz * (double) i / kSr), channel);
        if (i >= n / 2) sum += (double) y * y;
    }
    return std::sqrt(sum / (double) (n / 2));
}
}

struct VoiceDspTests : public juce::UnitTest
{
    VoiceDspTests() : juce::UnitTest("Voice DSP helpers", "mu-core") {}

    void runTest() override
    {
        beginTest("equal-power pan: the ends, the centre, constant power, and the range is held");
        {
            float l = 0, r = 0;
            mu_core::equalPowerPan(0.0f, l, r);
            expectWithinAbsoluteError(l, 1.0f, 1.0e-6f); expectWithinAbsoluteError(r, 1.0f, 1.0e-6f);   // the centre keeps a mono source's level
            mu_core::equalPowerPan(-1.0f, l, r);
            expectWithinAbsoluteError(l, 1.41421356f, 1.0e-5f); expectWithinAbsoluteError(r, 0.0f, 1.0e-5f);
            mu_core::equalPowerPan(1.0f, l, r);
            expectWithinAbsoluteError(l, 0.0f, 1.0e-5f); expectWithinAbsoluteError(r, 1.41421356f, 1.0e-5f);
            for (float p = -1.0f; p <= 1.0f; p += 0.05f)
            {
                mu_core::equalPowerPan(p, l, r);
                expectWithinAbsoluteError(l * l + r * r, 2.0f, 1.0e-4f);
            }
            float l2 = 0, r2 = 0;
            mu_core::equalPowerPan(-7.0f, l2, r2); mu_core::equalPowerPan(-1.0f, l, r);
            expectEquals(l2, l); expectEquals(r2, r);                                                   // out of range is clamped
        }

        beginTest("state-variable filter: a low-pass passes DC and cuts a tone an octave above, a high-pass blocks DC");
        {
            mu_core::TptSvf lp; lp.setTap(mu_core::TptSvf::Tap::lowpass); lp.setCoefficients(1000.0f, 0.0f, kSr);
            expectWithinAbsoluteError(sineRms(lp, 20.0), 0.5 / std::sqrt(2.0), 0.02);                  // far below the cutoff: passes
            lp.reset();
            expectLessThan(sineRms(lp, 4000.0), 0.25 * 0.5 / std::sqrt(2.0));                          // two octaves above: at least 12 dB down
            mu_core::TptSvf hp; hp.setTap(mu_core::TptSvf::Tap::highpass); hp.setCoefficients(1000.0f, 0.0f, kSr);
            expectLessThan(sineRms(hp, 20.0), 0.05);
            hp.reset();
            expectWithinAbsoluteError(sineRms(hp, 10000.0), 0.5 / std::sqrt(2.0), 0.03);
        }

        beginTest("state-variable filter: the band-pass peak is unity at any resonance, and is finite at the extremes");
        {
            for (float res : { 0.0f, 0.3f, 0.7f, 1.0f })
            {
                mu_core::TptSvf bp; bp.setTap(mu_core::TptSvf::Tap::bandpass); bp.setCoefficients(1000.0f, res, kSr);
                expectWithinAbsoluteError(sineRms(bp, 1000.0), 0.5 / std::sqrt(2.0), 0.03);
            }
            for (float cutoff : { -50.0f, 0.0f, 20.0f, 19999.0f, 1.0e9f })
                for (float res : { -1.0f, 0.0f, 1.0f, 5.0f })
                {
                    mu_core::TptSvf f; f.setCoefficients(cutoff, res, kSr);
                    bool finite = true;
                    for (int i = 0; i < 2000; ++i) finite = finite && std::isfinite(f.process(i % 2 ? 1.0f : -1.0f, 0));
                    expect(finite, "cutoff " + juce::String(cutoff) + " resonance " + juce::String(res));
                }
        }

        beginTest("state-variable filter: reset clears the state, the two channels are independent");
        {
            mu_core::TptSvf f; f.setCoefficients(500.0f, 0.5f, kSr);
            for (int i = 0; i < 100; ++i) f.process(1.0f, 0);
            expectWithinAbsoluteError(f.process(0.0f, 1), 0.0f, 1.0e-9f);                              // channel 1 never saw the input
            f.reset();
            expectWithinAbsoluteError(f.process(0.0f, 0), 0.0f, 1.0e-9f);
        }

        beginTest("age envelope: attack, decay, sustain, and no division by zero");
        {
            // 10 ms attack = 480 samples, 20 ms decay = 960 samples, sustain 0.4.
            expectWithinAbsoluteError(mu_core::adsAtAge(0.0, 10.0f, 20.0f, 0.4f, kSr), 0.0f, 1.0e-6f);
            expectWithinAbsoluteError(mu_core::adsAtAge(240.0, 10.0f, 20.0f, 0.4f, kSr), 0.5f, 1.0e-5f);
            expectWithinAbsoluteError(mu_core::adsAtAge(480.0, 10.0f, 20.0f, 0.4f, kSr), 1.0f, 1.0e-5f);
            expectWithinAbsoluteError(mu_core::adsAtAge(480.0 + 480.0, 10.0f, 20.0f, 0.4f, kSr), 0.7f, 1.0e-5f);
            expectWithinAbsoluteError(mu_core::adsAtAge(100000.0, 10.0f, 20.0f, 0.4f, kSr), 0.4f, 1.0e-6f);
            expectWithinAbsoluteError(mu_core::adsAtAge(5.0, 0.0f, 0.0f, 0.4f, kSr), 0.4f, 1.0e-6f);    // no attack, no decay: the sustain at once
            expectWithinAbsoluteError(mu_core::adsAtAge(0.0, 0.0f, 20.0f, 0.4f, kSr), 1.0f, 1.0e-6f);   // no attack: starts at full level
        }
    }
};

static VoiceDspTests voiceDspTests;
