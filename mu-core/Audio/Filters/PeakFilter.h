#pragma once

#include "FilterFamilies.h"

// type 12 — biquad peak / bell filter, fixed +12 dB gain. Centre
// frequency is the cutoff knob; Q scales from resonance.
inline void setPeakCoefficients(BiquadFilter& f, float hz, float q, float sr) { f.setPeak(hz, q, 12.0f, sr); }

class PeakFilter : public StereoBiquadFilter<setPeakCoefficients> {};
