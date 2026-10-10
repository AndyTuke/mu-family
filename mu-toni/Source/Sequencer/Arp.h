#pragma once

#include "Sequencer/ArpVoiceRunner.h"   // per-layer arpeggiator + ToniVoice
#include "Sequencer/Layer.h"            // mu-core: name, colour, modulation
#include "Audio/InsertProcessor.h"      // mu-core: shared per-voice insert FX
#include "Modulation/MuToniModDest.h"   // isValidModDest

namespace mu_toni
{

// One mu-Toni layer: the shared Layer (name, colour, modulation) plus its own arp voice and
// insert effect. `insertCfg` holds the insert's algorithm + slot params, set per block.
struct Arp : Layer
{
    ArpVoiceRunner  runner;
    InsertProcessor insert;
    VoiceParams     insertCfg;

    // Saved modulators keep only destinations mu-Toni has.
    bool isValidDest(const std::string& id) const override { return isValidModDest(id); }
};

} // namespace mu_toni
