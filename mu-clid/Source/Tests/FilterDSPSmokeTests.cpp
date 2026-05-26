// Filter algorithm DSP smoke tests.
//
// Verifies all 16 filter algorithms run without crash or NaN/Inf output when
// fed a stereo impulse at representative cutoff and resonance settings.
// These are not golden-master tests — exact DSP output is not asserted.
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
        beginTest ("algo 0: Lp12Filter — no crash, no NaN");
        { Lp12Filter f; smokeFilter(f, "Lp12Filter"); }

        beginTest ("algo 1: Hp12Filter — no crash, no NaN");
        { Hp12Filter f; smokeFilter(f, "Hp12Filter"); }

        beginTest ("algo 2: Bp12Filter — no crash, no NaN");
        { Bp12Filter f; smokeFilter(f, "Bp12Filter"); }

        beginTest ("algo 3: Notch12Filter — no crash, no NaN");
        { Notch12Filter f; smokeFilter(f, "Notch12Filter"); }

        beginTest ("algo 4: Lp24Filter — no crash, no NaN");
        { Lp24Filter f; smokeFilter(f, "Lp24Filter"); }

        beginTest ("algo 5: Hp24Filter — no crash, no NaN");
        { Hp24Filter f; smokeFilter(f, "Hp24Filter"); }

        beginTest ("algo 6: Bp24Filter — no crash, no NaN");
        { Bp24Filter f; smokeFilter(f, "Bp24Filter"); }

        beginTest ("algo 7: Lp6Filter — no crash, no NaN");
        { Lp6Filter f; smokeFilter(f, "Lp6Filter"); }

        beginTest ("algo 8: CombPlusFilter — no crash, no NaN");
        { CombPlusFilter f; smokeFilter(f, "CombPlusFilter", 220.0f, 0.5f); }

        beginTest ("algo 9: Ap12Filter — no crash, no NaN");
        { Ap12Filter f; smokeFilter(f, "Ap12Filter"); }

        beginTest ("algo 10: Notch24Filter — no crash, no NaN");
        { Notch24Filter f; smokeFilter(f, "Notch24Filter"); }

        beginTest ("algo 11: Hp6Filter — no crash, no NaN");
        { Hp6Filter f; smokeFilter(f, "Hp6Filter"); }

        beginTest ("algo 12: PeakFilter — no crash, no NaN");
        { PeakFilter f; smokeFilter(f, "PeakFilter"); }

        beginTest ("algo 13: LowShelfFilter — no crash, no NaN");
        { LowShelfFilter f; smokeFilter(f, "LowShelfFilter"); }

        beginTest ("algo 14: HighShelfFilter — no crash, no NaN");
        { HighShelfFilter f; smokeFilter(f, "HighShelfFilter"); }

        beginTest ("algo 15: CombMinusFilter — no crash, no NaN");
        { CombMinusFilter f; smokeFilter(f, "CombMinusFilter", 220.0f, 0.5f); }

        // ── High-resonance stress test ────────────────────────────────────────
        // SVF-based filters should stay stable even at resonance=0.99.
        beginTest ("Lp12Filter: high resonance (0.99) — no crash, no NaN");
        { Lp12Filter f; smokeFilter(f, "Lp12Filter-HiRes", 2000.0f, 0.99f); }

        beginTest ("Hp12Filter: high resonance (0.99) — no crash, no NaN");
        { Hp12Filter f; smokeFilter(f, "Hp12Filter-HiRes", 2000.0f, 0.99f); }

        beginTest ("Bp12Filter: high resonance (0.99) — no crash, no NaN");
        { Bp12Filter f; smokeFilter(f, "Bp12Filter-HiRes", 2000.0f, 0.99f); }

        // ── Extreme cutoff frequencies ────────────────────────────────────────
        beginTest ("Lp12Filter: low cutoff (20 Hz) — no crash, no NaN");
        { Lp12Filter f; smokeFilter(f, "Lp12Filter-LowCut", 20.0f, 0.3f); }

        beginTest ("Lp12Filter: high cutoff (20 kHz) — no crash, no NaN");
        { Lp12Filter f; smokeFilter(f, "Lp12Filter-HiCut", 20000.0f, 0.3f); }
    }
};

static FilterDSPSmokeTest filterDSPSmokeTest;
