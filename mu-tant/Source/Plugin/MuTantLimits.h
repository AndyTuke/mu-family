#pragma once

#include "MuLimits.h"

namespace mu_tant
{

// The counts this product is built with. They go to ProcessorBase and to the parameter layout (so the two cannot disagree), and the
// product's own per-layer arrays are sized from them. mu-Clid MIDI-style products with no layers do not have one.
inline constexpr mu_limits::ProcessorLimits kLimits { 8, 0, 2, true };

} // namespace mu_tant
