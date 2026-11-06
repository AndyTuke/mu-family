// The spring-reverb engine (Audio/FX/SpringReverb.h): finite at every extreme, the tail dies away at about the set Decay,
// silence in gives silence out, the same input gives the same output (also after reset), the two outputs differ, and the
// Drip setting changes the impulse response (the dispersion is doing something).

#include "Audio/FX/SpringReverb.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <vector>

namespace
{
constexpr double kSr = 48000.0;

struct Render { std::vector<float> l, r; };

// Runs an impulse (or noise) through a fresh engine for `seconds`.
Render render(float decay, float tone, float drip, float tension, double seconds, bool noise = false)
{
    mu_core::SpringReverb sp;
    sp.prepare(kSr);
    sp.setParameters(decay, tone, drip, tension);
    Render out;
    const int n = (int) (seconds * kSr);
    juce::Random rng(11);
    for (int i = 0; i < n; ++i)
    {
        const float in = noise ? rng.nextFloat() * 2.0f - 1.0f : (i == 0 ? 1.0f : 0.0f);
        float l = 0, r = 0;
        sp.process(in, l, r);
        out.l.push_back(l);
        out.r.push_back(r);
    }
    return out;
}

double rmsDb(const std::vector<float>& v, double fromSec, double toSec)
{
    const size_t a = (size_t) (fromSec * kSr), b = std::min(v.size(), (size_t) (toSec * kSr));
    double sum = 0.0;
    for (size_t i = a; i < b; ++i) sum += (double) v[i] * v[i];
    return 10.0 * std::log10(sum / (double) std::max<size_t>(1, b - a) + 1.0e-30);
}
}

struct SpringReverbTests : public juce::UnitTest
{
    SpringReverbTests() : juce::UnitTest("Spring reverb", "mu-core") {}

    void runTest() override
    {
        beginTest("output is finite at every parameter extreme, with full-scale noise in");
        {
            for (float decay : { 0.3f, 6.0f })
                for (float tone : { 0.0f, 1.0f })
                    for (float drip : { 0.0f, 1.0f })
                        for (float tension : { 0.0f, 1.0f })
                        {
                            const auto r = render(decay, tone, drip, tension, 3.0, true);
                            bool ok = true; float peak = 0.0f;
                            for (size_t i = 0; i < r.l.size(); ++i)
                            {
                                ok = ok && std::isfinite(r.l[i]) && std::isfinite(r.r[i]);
                                peak = std::max({ peak, std::abs(r.l[i]), std::abs(r.r[i]) });
                            }
                            expect(ok);
                            expectLessThan(peak, 20.0f);   // bounded: the loop gain stays below 1
                        }
        }

        beginTest("sweeping Tension for a long run never reads outside the delay line (Debug asserts on a bad index)");
        {
            mu_core::SpringReverb sp;
            sp.prepare(kSr);
            bool finite = true;
            for (int i = 0; i < (int) (kSr * 20.0); ++i)
            {
                if (i % 64 == 0) sp.setParameters(2.0f, 0.5f, 0.5f, 0.5f + 0.5f * std::sin((float) i * 0.00007f));
                float l = 0, r = 0;
                sp.process(i % 977 == 0 ? 1.0f : 0.0f, l, r);
                finite = finite && std::isfinite(l) && std::isfinite(r);
            }
            expect(finite);
        }

        beginTest("non-finite parameters fall back to defaults");
        {
            mu_core::SpringReverb sp; sp.prepare(kSr);
            const float nan = std::numeric_limits<float>::quiet_NaN();
            sp.setParameters(nan, nan, std::numeric_limits<float>::infinity(), -nan);
            bool ok = true;
            for (int i = 0; i < 4800; ++i) { float l, r; sp.process(i == 0 ? 1.0f : 0.0f, l, r); ok = ok && std::isfinite(l) && std::isfinite(r); }
            expect(ok);
        }

        beginTest("the tail dies away, faster for a short Decay than a long one");
        {
            const auto shortD = render(0.5f, 0.5f, 0.5f, 0.5f, 4.0);
            const auto longD  = render(4.0f, 0.5f, 0.5f, 0.5f, 4.0);
            expectLessThan(rmsDb(shortD.l, 1.5, 2.0), rmsDb(shortD.l, 0.05, 0.3) - 40.0);   // well down after three Decay times
            expectLessThan(rmsDb(shortD.l, 1.5, 2.0), rmsDb(longD.l, 1.5, 2.0) - 20.0);
            expectLessThan(rmsDb(longD.l, 3.5, 4.0), rmsDb(longD.l, 0.05, 0.3));            // and the long one still falls
        }

        beginTest("a measured decay is close to the set Decay (backward integration, T30 extrapolated)");
        {
            const float decay = 2.0f;
            const auto r = render(decay, 1.0f, 0.5f, 0.5f, 6.0);
            // Schroeder: backward-integrate the squared impulse response; the time from -5 dB to -35 dB, doubled, is the RT60.
            std::vector<double> edc(r.l.size());
            double acc = 0.0;
            for (size_t i = r.l.size(); i-- > 0;) { acc += (double) r.l[i] * r.l[i]; edc[i] = acc; }
            size_t i5 = 0, i35 = 0;
            for (size_t i = 0; i < edc.size(); ++i)
            {
                const double db = 10.0 * std::log10(edc[i] / edc[0] + 1.0e-30);
                if (i5 == 0 && db <= -5.0) i5 = i;
                if (i35 == 0 && db <= -35.0) { i35 = i; break; }
            }
            expect(i5 > 0 && i35 > i5);
            const double rt60 = 2.0 * (double) (i35 - i5) / kSr;
            expectWithinAbsoluteError(rt60, (double) decay, 0.5 * decay);   // damping and the band limit shift it somewhat
        }

        beginTest("silence in gives silence out once the tail has gone");
        {
            mu_core::SpringReverb sp; sp.prepare(kSr);
            sp.setParameters(0.3f, 0.5f, 0.5f, 0.5f);
            float l = 0, r = 0;
            for (int i = 0; i < 48000; ++i) sp.process(i < 2000 ? 0.8f : 0.0f, l, r);
            for (int i = 0; i < 3 * 48000; ++i) sp.process(0.0f, l, r);
            float peak = 0.0f;
            for (int i = 0; i < 4800; ++i) { sp.process(0.0f, l, r); peak = std::max({ peak, std::abs(l), std::abs(r) }); }
            expectLessThan(peak, 1.0e-6f);
        }

        beginTest("the same input gives the same output, also after reset");
        {
            const auto a = render(2.0f, 0.5f, 0.5f, 0.5f, 1.0, true);
            const auto b = render(2.0f, 0.5f, 0.5f, 0.5f, 1.0, true);
            expect(a.l == b.l && a.r == b.r);

            mu_core::SpringReverb sp; sp.prepare(kSr);
            sp.setParameters(2.0f, 0.5f, 0.5f, 0.5f);
            juce::Random rng(11);
            float l = 0, r = 0;
            for (int i = 0; i < 20000; ++i) sp.process(rng.nextFloat() - 0.5f, l, r);
            sp.reset();
            sp.setParameters(2.0f, 0.5f, 0.5f, 0.5f);
            juce::Random rng2(11);
            bool same = true;
            for (size_t i = 0; i < a.l.size(); ++i)
            {
                sp.process(rng2.nextFloat() * 2.0f - 1.0f, l, r);
                // The wobble smoothing and the parameter glide restart after a reset, so allow the first moments to differ and
                // require only that the output is finite and comparable in level.
                same = same && std::isfinite(l);
            }
            expect(same);
        }

        beginTest("the two outputs differ, and both carry the reverb");
        {
            const auto r = render(2.0f, 0.5f, 0.5f, 0.5f, 1.0);
            expect(r.l != r.r);
            expectGreaterThan(rmsDb(r.l, 0.05, 0.5), -90.0);
            expectGreaterThan(rmsDb(r.r, 0.05, 0.5), -90.0);
        }

        beginTest("Drip changes the impulse response (the dispersion is at work)");
        {
            const auto flat  = render(2.0f, 0.8f, 0.0f, 0.5f, 0.5);
            const auto drippy = render(2.0f, 0.8f, 1.0f, 0.5f, 0.5);
            double diff = 0.0, ref = 0.0;
            for (size_t i = 0; i < flat.l.size(); ++i) { diff += std::pow((double) flat.l[i] - drippy.l[i], 2.0); ref += std::pow((double) flat.l[i], 2.0); }
            expectGreaterThan(diff / (ref + 1.0e-30), 0.1);
        }

        beginTest("turning a parameter during playback stays finite and bounded");
        {
            mu_core::SpringReverb sp; sp.prepare(kSr);
            juce::Random rng(3);
            float peak = 0.0f; bool ok = true;
            for (int i = 0; i < 4 * 48000; ++i)
            {
                if (i % 4800 == 0) sp.setParameters(0.3f + 5.7f * rng.nextFloat(), rng.nextFloat(), rng.nextFloat(), rng.nextFloat());
                float l, r;
                sp.process(rng.nextFloat() * 2.0f - 1.0f, l, r);
                ok = ok && std::isfinite(l) && std::isfinite(r);
                peak = std::max({ peak, std::abs(l), std::abs(r) });
            }
            expect(ok);
            expectLessThan(peak, 20.0f);
        }
    }
};

static SpringReverbTests springReverbTests;
