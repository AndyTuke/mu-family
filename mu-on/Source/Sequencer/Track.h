#pragma once

#include "Sequencer/Layer.h"   // mu-core: per-lane ControlSequences + ModulationMatrix

#include <atomic>

namespace mu_on
{

// One mu-On lane (Kick / Bass / Hat / Snare / Rumble): the shared Layer (name, colour, modulation)
// plus the trigger counter the audio thread bumps and the editor polls to pulse the sidebar lane.
struct Track : Layer
{
    std::atomic<int> triggers { 0 };
};

} // namespace mu_on
