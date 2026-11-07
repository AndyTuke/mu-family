#pragma once

#include "Modulation/ModTarget.h"         // mu-core: the standard target row

#include <string>

// μ-Toni MIDI's modulation targets: the arpeggiator rows of mu-Toni's table (the same ids and the same a0_ parameters, so a
// modulator saved here means the same thing in mu-Toni), without the synth, filter, envelope, insert and mixer rows a MIDI effect
// has not got. Depth is a percentage of the target's range (the family standard, resolved by mu_mod::resolveLane).
namespace mu_toni_midi
{

using ModDest = mu_mod::ModTarget;

inline constexpr ModDest kModDestTable[] = {
    { "arp.dir",    "Direction",   "Arp", "dir" },
    { "arp.octs",   "Octaves",     "Arp", "octs" },
    { "arp.inv",    "Inversion",   "Arp", "inv" },
    { "arp.chord",  "Chord",       "Arp", "chord" },
    { "arp.root",   "Root",        "Arp", "root" },
    { "arp.roct",   "Root Octave", "Arp", "roct" },
    { "arp.rate",   "Rate",        "Arp", "rate" },
    { "arp.gate",   "Gate Length", "Arp", "gate" },
    { "arp.accent", "Accent",      "Arp", "acc" },
};

// The out[] indices: MUST match kModDestTable order.
enum ModDestIndex { D_dir, D_octs, D_inv, D_chord, D_root, D_roct, D_rate, D_gate, D_acc, kNumModDests };

static_assert((int) (sizeof(kModDestTable) / sizeof(kModDestTable[0])) == kNumModDests, "kModDestTable size must equal kNumModDests");

inline bool isValidModDest(const std::string& id)
{
    for (const auto& d : kModDestTable) if (id == d.id) return true;
    return false;
}

} // namespace mu_toni_midi
