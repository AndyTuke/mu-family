// Filter algorithm DSP smoke tests.
//
// Verifies all 16 filter algorithms run without crash or NaN/Inf output when
// fed a stereo impulse at representative cutoff and resonance settings.
// These are not golden-master tests - exact DSP output is not asserted.
// Goals: (a) no crash, (b) no NaN/Inf in output buffer.

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "Audio/Filters/Lp12Filter.h"
#include "Audio/Filters/Hp12Filter.h"
#include "Audio/Filters/Bp12Filter.h"
#include "Audio/Filters/Notch12Filter.h"
#include "Audio/Filters/Lp24Filter.h"
#include "Audio/Filters/Hp24Filter.h"
#include "Audio/Filters/Bp24Filter.h"
#include "Audio/Filters/Lp6Filter.h"
#include "Audio/Filters/CombPlusFilter.h"
#include "Audio/Filters/Ap12Filter.h"
#include "Audio/Filters/Notch24Filter.h"
#include "Audio/Filters/Hp6Filter.h"
#include "Audio/Filters/PeakFilter.h"
#include "Audio/Filters/LowShelfFilter.h"
#include "Audio/Filters/HighShelfFilter.h"
#include "Audio/Filters/CombMinusFilter.h"
#include "Audio/MultiModeFilter.h"
class FilterDSPSmokeTest : public juce::UnitTest
{
public:
    FilterDSPSmokeTest() : juce::UnitTest ("Filter DSP smoke", "DSP") {}

    void smokeFilter (FilterAlgorithmBase& flt, const char* algoName,
                      float cutoffHz = 1000.0f, float resonance = 0.3f,
                      double sr = 48000.0, int ns = 512)
    {
        flt.prepare(sr, ns, 2);

        juce::AudioBuffer<float> buf(2, ns);
        buf.clear();
        buf.setSample(0, 0, 0.5f);
        buf.setSample(1, 0, 0.5f);

        flt.process(buf, ns, 2, cutoffHz, resonance);

        for (int ch = 0; ch < 2; ++ch)
        {
            const float* d = buf.getReadPointer(ch);
            for (int i = 0; i < ns; ++i)
                expect (std::isfinite(d[i]),
                        juce::String(algoName) + " ch" + juce::String(ch)
                        + " sample " + juce::String(i) + " is non-finite");
        }
    }

    void runTest() override
    {
        beginTest ("algo 0: Lp12Filter - no crash, no NaN");
        { Lp12Filter f; smokeFilter(f, "Lp12Filter"); }

        beginTest ("algo 1: Hp12Filter - no crash, no NaN");
        { Hp12Filter f; smokeFilter(f, "Hp12Filter"); }

        beginTest ("algo 2: Bp12Filter - no crash, no NaN");
        { Bp12Filter f; smokeFilter(f, "Bp12Filter"); }

        beginTest ("algo 3: Notch12Filter - no crash, no NaN");
        { Notch12Filter f; smokeFilter(f, "Notch12Filter"); }

        beginTest ("algo 4: Lp24Filter - no crash, no NaN");
        { Lp24Filter f; smokeFilter(f, "Lp24Filter"); }

        beginTest ("algo 5: Hp24Filter - no crash, no NaN");
        { Hp24Filter f; smokeFilter(f, "Hp24Filter"); }

        beginTest ("algo 6: Bp24Filter - no crash, no NaN");
        { Bp24Filter f; smokeFilter(f, "Bp24Filter"); }

        beginTest ("algo 7: Lp6Filter - no crash, no NaN");
        { Lp6Filter f; smokeFilter(f, "Lp6Filter"); }

        beginTest ("algo 8: CombPlusFilter - no crash, no NaN");
        { CombPlusFilter f; smokeFilter(f, "CombPlusFilter", 220.0f, 0.5f); }

        beginTest ("algo 9: Ap12Filter - no crash, no NaN");
        { Ap12Filter f; smokeFilter(f, "Ap12Filter"); }

        beginTest ("algo 10: Notch24Filter - no crash, no NaN");
        { Notch24Filter f; smokeFilter(f, "Notch24Filter"); }

        beginTest ("algo 11: Hp6Filter - no crash, no NaN");
        { Hp6Filter f; smokeFilter(f, "Hp6Filter"); }

        beginTest ("algo 12: PeakFilter - no crash, no NaN");
        { PeakFilter f; smokeFilter(f, "PeakFilter"); }

        beginTest ("algo 13: LowShelfFilter - no crash, no NaN");
        { LowShelfFilter f; smokeFilter(f, "LowShelfFilter"); }

        beginTest ("algo 14: HighShelfFilter - no crash, no NaN");
        { HighShelfFilter f; smokeFilter(f, "HighShelfFilter"); }

        beginTest ("algo 15: CombMinusFilter - no crash, no NaN");
        { CombMinusFilter f; smokeFilter(f, "CombMinusFilter", 220.0f, 0.5f); }

        // ── High-resonance stress test ────────────────────────────────────────
        // SVF-based filters should stay stable even at resonance=0.99.
        beginTest ("Lp12Filter: high resonance (0.99) - no crash, no NaN");
        { Lp12Filter f; smokeFilter(f, "Lp12Filter-HiRes", 2000.0f, 0.99f); }

        beginTest ("Hp12Filter: high resonance (0.99) - no crash, no NaN");
        { Hp12Filter f; smokeFilter(f, "Hp12Filter-HiRes", 2000.0f, 0.99f); }

        beginTest ("Bp12Filter: high resonance (0.99) - no crash, no NaN");
        { Bp12Filter f; smokeFilter(f, "Bp12Filter-HiRes", 2000.0f, 0.99f); }

        // ── Extreme cutoff frequencies ────────────────────────────────────────
        beginTest ("MultiModeFilter: resetSmoothers makes a reused filter render like a fresh one");
        {
            const double sr = 48000.0; const int ns = 512;
            juce::Random rng(11);
            juce::AudioBuffer<float> in(1, ns);
            for (int i = 0; i < ns; ++i) in.setSample(0, i, rng.nextFloat() * 0.8f - 0.4f);

            auto configure = [](MultiModeFilter& f, float drive, float lowCut)
            {
                f.setType(0); f.setCutoff(2000.0f); f.setResonance(0.2f);
                f.setDrive(drive); f.setLowCut(lowCut);
            };

            // The previous tenant: full drive and a high low-cut, run until the smoothers settle there.
            MultiModeFilter used;  used.prepare(sr, ns, 1);
            configure(used, 1.0f, 800.0f);
            for (int b = 0; b < 8; ++b) { juce::AudioBuffer<float> t(in); used.process(t, ns, 1); }

            used.reset();
            used.resetSmoothers();

            // The new tenant's settings (low-cut off: the low-cut ladder keeps its own internal smoothing,
            // which this does not reset); the fresh filter starts from the prepared state.
            MultiModeFilter fresh; fresh.prepare(sr, ns, 1);
            configure(used, 0.3f, 0.0f);
            configure(fresh, 0.3f, 0.0f);

            juce::AudioBuffer<float> a(in), b(in);
            used.process(a, ns, 1);
            fresh.process(b, ns, 1);
            float maxDiff = 0.0f;
            for (int i = 0; i < ns; ++i) maxDiff = juce::jmax(maxDiff, std::abs(a.getSample(0, i) - b.getSample(0, i)));
            expectLessThan(maxDiff, 1.0e-6f, "a reused filter must not carry the old drive / low-cut ramp");
        }

        beginTest ("MultiModeFilter: a reused filter with the low-cut on renders like a fresh one");
        {
            const double sr = 48000.0; const int ns = 512;
            juce::Random rng(12);
            juce::AudioBuffer<float> in(1, ns);
            for (int i = 0; i < ns; ++i) in.setSample(0, i, rng.nextFloat() * 0.8f - 0.4f);

            auto configure = [](MultiModeFilter& f, float drive, float lowCut)
            {
                f.setType(0); f.setCutoff(2000.0f); f.setResonance(0.2f);
                f.setDrive(drive); f.setLowCut(lowCut);
            };

            // The previous tenant ran the low-cut at a high setting until its smoothers settled there.
            MultiModeFilter used;  used.prepare(sr, ns, 1);
            configure(used, 1.0f, 2500.0f);
            for (int b = 0; b < 16; ++b) { juce::AudioBuffer<float> t(in); used.process(t, ns, 1); }

            used.reset();
            used.resetSmoothers();

            // The new tenant also uses the low-cut, so the ladder's own cutoff ramp (50 ms) is audible.
            MultiModeFilter fresh; fresh.prepare(sr, ns, 1);
            configure(used, 0.3f, 150.0f);
            configure(fresh, 0.3f, 150.0f);

            float maxDiff = 0.0f;
            for (int b = 0; b < 8; ++b)   // 8 x 512 samples = 85 ms, past the ladder's ramp
            {
                juce::AudioBuffer<float> a(in), c(in);
                used.process(a, ns, 1);
                fresh.process(c, ns, 1);
                for (int i = 0; i < ns; ++i) maxDiff = juce::jmax(maxDiff, std::abs(a.getSample(0, i) - c.getSample(0, i)));
            }
            logMessage("low-cut reused-vs-fresh max difference: " + juce::String(maxDiff));
            expectLessThan(maxDiff, 1.0e-6f, "a reused filter must not carry the old low-cut ladder cutoff ramp");
        }

        beginTest ("Lp12Filter: low cutoff (20 Hz) - no crash, no NaN");
        { Lp12Filter f; smokeFilter(f, "Lp12Filter-LowCut", 20.0f, 0.3f); }

        beginTest ("Lp12Filter: high cutoff (20 kHz) - no crash, no NaN");
        { Lp12Filter f; smokeFilter(f, "Lp12Filter-HiCut", 20000.0f, 0.3f); }
    }
};

static FilterDSPSmokeTest filterDSPSmokeTest;

// A block longer than the size the comb was prepared for is filtered all the way to its end: the result
// matches the same signal fed as prepared-size blocks.
class CombOversizeBlockTest : public juce::UnitTest
{
public:
    CombOversizeBlockTest() : juce::UnitTest ("Comb filter oversize block", "DSP") {}

    template <typename Comb>
    void check (const char* name)
    {
        constexpr double kSr = 48000.0;
        constexpr int kPrepared = 128, kTotal = 128 * 5 + 37;   // not a multiple of the prepared size
        juce::AudioBuffer<float> big (2, kTotal), split (2, kTotal);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kTotal; ++i)
            {
                const float x = 0.5f * std::sin (0.03f * (float) i * (float) (ch + 1)) + (i == 3 ? 0.4f : 0.0f);
                big.setSample (ch, i, x);
                split.setSample (ch, i, x);
            }

        Comb a, b;
        a.prepare (kSr, kPrepared, 2);
        b.prepare (kSr, kPrepared, 2);
        a.process (big, kTotal, 2, 800.0f, 0.7f);
        for (int start = 0; start < kTotal; start += kPrepared)
        {
            const int n = juce::jmin (kPrepared, kTotal - start);
            juce::AudioBuffer<float> chunk (split.getArrayOfWritePointers(), 2, start, n);
            b.process (chunk, n, 2, 800.0f, 0.7f);
        }

        float maxDiff = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kTotal; ++i)
                maxDiff = juce::jmax (maxDiff, std::abs (big.getSample (ch, i) - split.getSample (ch, i)));
        expectLessThan (maxDiff, 1.0e-6f, juce::String (name) + ": the oversize block differs from prepared-size blocks");
    }

    void runTest() override
    {
        beginTest ("CombPlusFilter: oversize block equals prepared-size blocks");
        check<CombPlusFilter> ("CombPlusFilter");
        beginTest ("CombMinusFilter: oversize block equals prepared-size blocks");
        check<CombMinusFilter> ("CombMinusFilter");
    }
};

static CombOversizeBlockTest combOversizeBlockTest;
