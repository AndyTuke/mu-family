#pragma once

#include "FilterFamilies.h"

// type 15 — feedback comb with NEGATIVE feedback (Karplus-Strong feel).
//
//   y[n] = x[n] − g·y[n-D]   where D = sample_rate / cutoffHz
//
// The sign flip moves the resonant peaks to ODD multiples of f0/2 (f0/2,
// 3f0/2, 5f0/2…). Sounds darker / more "stringy" than the positive-feedback
// variant — the same delay-line, just inverted in the loop.
class CombMinusFilter : public CombFeedbackFilter<-1> {};
