#pragma once

#include <juce_core/juce_core.h>
#include <cmath>

// Proportion-space skew conversions for modulation. Skewed-slider
// destinations (ADSR times, filter cutoff, filter low-cut) are modulated in the
// slider's *proportion* space (0..1) so a given modulation depth covers the same
// visual arc regardless of knob position; the value is seeded with propFrom*()
// and converted back with the matching *FromProp() at write-back. Extracted from
// PluginProcessor::applyRhythmModulation where the same formulas
// were triplicated across the seed / UI-snapshot / write-back blocks, so the
// forward and inverse can be unit-tested as a true round-trip (test C5).
//
// Each pair is a mutual inverse on [0,1] / the destination's range:
//   ADSR   : skewFactor 0.3  on 0..10 s
//   lowCut : skewFactor 0.35 on 0..1000 Hz
//   cutoff : setSkewFactorFromMidPoint(640) on 20..20000 Hz  (~skewFactor 0.2):
//            proportion = pow((hz - 20) / 19980, 0.2)
namespace mu_clid::mod_skew
{

inline float propFromAdsr   (float secs) { return std::pow (juce::jlimit (0.0f, 1.0f, secs / 10.0f),   0.3f); }
inline float adsrFromProp   (float p)    { return 10.0f     * std::pow (juce::jlimit (0.0f, 1.0f, p), 1.0f / 0.3f); }

inline float propFromLowCut (float hz)   { return std::pow (juce::jlimit (0.0f, 1.0f, hz / 1000.0f),   0.35f); }
inline float lowCutFromProp (float p)    { return 1000.0f   * std::pow (juce::jlimit (0.0f, 1.0f, p), 1.0f / 0.35f); }

inline float propFromCutoff (float hz)   { return std::pow (juce::jlimit (0.0f, 1.0f, (hz - 20.0f) / 19980.0f), 0.2f); }
inline float cutoffFromProp (float p)    { return 20.0f + 19980.0f * std::pow (juce::jlimit (0.0f, 1.0f, p), 1.0f / 0.2f); }

} // namespace mu_clid::mod_skew
