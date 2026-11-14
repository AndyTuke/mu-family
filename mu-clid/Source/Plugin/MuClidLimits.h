#pragma once

#include "MuLimits.h"

namespace mu_clid
{

// The counts this product is built with. They go to ProcessorBase and to the parameter layout (so the two cannot disagree), and the
// product's own per-layer arrays are sized from them. mu-Clid MIDI-style products with no layers do not have one.
inline constexpr mu_limits::ProcessorLimits kLimits { 8, 0, 2, true };

// Until the build-setting macros are retired this must equal them (the mu-core library is still built from the macros).
static_assert(kLimits.layers == mu_limits::kMaxLayers && kLimits.buses == mu_limits::kMaxBuses
              && kLimits.busInserts == mu_limits::kBusInserts && kLimits.externalSidechain == mu_limits::kExternalSidechain,
              "the product's limits differ from the mu-core build settings");

} // namespace mu_clid
