#pragma once

#include "FilterFamilies.h"

// type 8 — feedback comb with POSITIVE feedback.
//
//   y[n] = x[n] + g·y[n-D]   where D = sample_rate / cutoffHz
//
// Resonances pile up at f0, 2f0, 3f0… (the integer harmonics of f0). Good for
// tuned drones, plucked-string textures, and metallic resonator effects.
// The delay buffer is sized for the lowest pitch (20 Hz cutoff floor) and the
// read tap uses linear interpolation between adjacent samples so the resonant
// frequency is continuous-valued, not quantised to integer delay lengths.
class CombPlusFilter : public CombFeedbackFilter<+1> {};
