#pragma once

#include "Sequencer/Layer.h"   // mu-core: name, colour, modulation

namespace mu_tant
{

// One mu-Tant layer (a drone voice): today just the shared Layer. The per-voice gate / filter /
// pitch patterns and engine state move in here as the layer plan progresses.
struct Pattern : Layer
{
};

} // namespace mu_tant
