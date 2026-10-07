#pragma once

#include "FilterFamilies.h"

// type 13 — biquad low shelf, fixed +12 dB gain. Cutoff is the corner
// frequency below which the shelf takes effect.
inline void setLowShelfCoefficients(BiquadFilter& f, float hz, float q, float sr) { f.setLowShelf(hz, q, 12.0f, sr); }

class LowShelfFilter : public StereoBiquadFilter<setLowShelfCoefficients> {};
