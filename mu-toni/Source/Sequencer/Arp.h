#pragma once

#include "Sequencer/ArpVoiceRunner.h"   // per-layer arpeggiator + ToniVoice
#include "Sequencer/Layer.h"            // mu-core: name, colour, modulation
#include "Audio/InsertProcessor.h"      // mu-core: shared per-voice insert FX

namespace mu_toni
{

// One mu-Toni layer: the shared Layer (name, colour, modulation) plus its own arp voice and
// insert effect. `insertCfg` holds the insert's algorithm + slot params, set per block.
struct Arp : Layer
{
    ArpVoiceRunner  runner;
    InsertProcessor insert;
    VoiceParams     insertCfg;
};

} // namespace mu_toni
