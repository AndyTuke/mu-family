// C5 - proportion-space modulation skew round-trips (#638/#639).
//
// Skewed-slider modulation seeds a value into proportion space with propFrom*()
// and converts it back with *FromProp(). These were triplicated inline lambdas in
// applyRhythmModulation (seed / UI-snapshot / write-back); after extraction into
// ModulationSkew.h this test guards that the forward and inverse remain mutual
// inverses, so a one-sided edit to a skew formula can't silently desync the
// seed from the write-back (which would shift every modulated value).

#include <juce_core/juce_core.h>
#include "Plugin/ModulationSkew.h"

class ModulationSkewTest : public juce::UnitTest
{
public:
    ModulationSkewTest() : juce::UnitTest ("Modulation skew round-trips", "ModulationSkew") {}

    void runTest() override
    {
        using namespace mu_mod_skew;

        beginTest ("C5: ADSR seconds survive prop -> actual -> prop (0..10 s, skew 0.3)");
        for (float s : { 0.01f, 0.1f, 0.5f, 1.0f, 3.0f, 7.5f, 10.0f })
            expectWithinAbsoluteError (adsrFromProp (propFromAdsr (s)), s,
                                       juce::jmax (0.01f, s * 0.01f),
                                       "ADSR round-trip at " + juce::String (s) + " s");

        beginTest ("C5: filter low-cut Hz round-trip (0..1000 Hz, skew 0.35)");
        for (float hz : { 1.0f, 50.0f, 200.0f, 500.0f, 1000.0f })
            expectWithinAbsoluteError (lowCutFromProp (propFromLowCut (hz)), hz,
                                       juce::jmax (0.5f, hz * 0.01f),
                                       "low-cut round-trip at " + juce::String (hz) + " Hz");

        beginTest ("C5: filter cutoff Hz round-trip (20..20000 Hz, skew ~0.2)");
        for (float hz : { 20.0f, 100.0f, 640.0f, 2000.0f, 8000.0f, 20000.0f })
            expectWithinAbsoluteError (cutoffFromProp (propFromCutoff (hz)), hz,
                                       juce::jmax (1.0f, hz * 0.01f),
                                       "cutoff round-trip at " + juce::String (hz) + " Hz");

        beginTest ("C5: proportion survives actual -> prop for all three skews");
        for (float p : { 0.0f, 0.1f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            expectWithinAbsoluteError (propFromAdsr   (adsrFromProp   (p)), p, 1.0e-3f, "ADSR prop "   + juce::String (p));
            expectWithinAbsoluteError (propFromLowCut (lowCutFromProp (p)), p, 1.0e-3f, "low-cut prop " + juce::String (p));
            expectWithinAbsoluteError (propFromCutoff (cutoffFromProp (p)), p, 1.0e-3f, "cutoff prop "  + juce::String (p));
        }

        beginTest ("C5: each inverse is monotonic increasing");
        expect (adsrFromProp   (0.3f) < adsrFromProp   (0.7f), "ADSR inverse monotonic");
        expect (lowCutFromProp (0.3f) < lowCutFromProp (0.7f), "low-cut inverse monotonic");
        expect (cutoffFromProp (0.3f) < cutoffFromProp (0.7f), "cutoff inverse monotonic");
    }
};

static ModulationSkewTest modulationSkewTest;
