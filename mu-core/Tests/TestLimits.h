#pragma once

#include "MuLimits.h"
#include "Audio/SidechainRemap.h"

// The counts mu-core's own tests build their processors and mixers with. mu-core is compiled once and takes its counts at
// construction (mu_limits::ProcessorLimits), so these tests choose theirs here: the three executables in Tests/CMakeLists.txt
// (mu-core-tests, -16, -bus1) differ only in these MU_TEST_* values, which belong to the test build and are read nowhere else.
// A product's own tests use the product's kLimits instead.
#ifndef MU_TEST_LAYERS
 #define MU_TEST_LAYERS 8
#endif
#ifndef MU_TEST_BUSES
 #define MU_TEST_BUSES 0
#endif
#ifndef MU_TEST_BUS_INSERTS
 #define MU_TEST_BUS_INSERTS 2
#endif
#ifndef MU_TEST_EXT_SIDECHAIN
 #define MU_TEST_EXT_SIDECHAIN 1
#endif

namespace mu_test
{
    inline constexpr mu_limits::ProcessorLimits kLimits { MU_TEST_LAYERS, MU_TEST_BUSES, MU_TEST_BUS_INSERTS, MU_TEST_EXT_SIDECHAIN != 0 };
    static_assert(kLimits.valid(), "the MU_TEST_* counts must be inside the ceilings");

    // The sidechain source values for this build's counts (the mixer's own extSidechainSrc() / busSidechainSource()).
    inline constexpr int kExtSidechainSrc = kLimits.layers;
    inline constexpr int busSidechainSource(int bus) noexcept { return mu_mix::busSource(bus, kLimits.layers); }
}
