#pragma once

#include "FilterFamilies.h"

// type 14 — biquad high shelf, fixed +12 dB gain. Cutoff is the corner
// frequency above which the shelf takes effect.
inline void setHighShelfCoefficients(BiquadFilter& f, float hz, float q, float sr) { f.setHighShelf(hz, q, 12.0f, sr); }

class HighShelfFilter : public StereoBiquadFilter<setHighShelfCoefficients> {};
