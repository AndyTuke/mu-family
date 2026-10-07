#pragma once

#include "FilterFamilies.h"

// type 9 — 12 dB/oct all-pass via the project's `BiquadFilter`
// primitive. Doesn't change magnitude (within the filter's passband) but
// shifts phase around the cutoff — useful for phaser-style effects when
// chained with a wet/dry mix or used as part of a serial filter network.
inline void setAp12Coefficients(BiquadFilter& f, float hz, float q, float sr) { f.setAllPass(hz, q, sr); }

class Ap12Filter : public StereoBiquadFilter<setAp12Coefficients> {};
