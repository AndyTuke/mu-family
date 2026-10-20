#pragma once

#include "Sequencer/Arp.h"
#include "Sequencer/LayerTypeInfo.h"        // mu-core: mu_core::LayerTypeInfo
#include "Persistence/ArpParams.h"          // addLayerParams
#include "Audio/NoiseSeed.h"                // mu-core: makeNoiseSalt
#include "Audio/Wavetable/WavetableBank.h"  // mu-core: the oscillators' tables

#include <memory>

// The Arp layer type's description for a host that offers several layer types (docs/mu-tronic/design.md 2.5),
// as mu-On's TrackTypes.h does for its five. Header-only on purpose: only a host that registers the type
// compiles it, so the library's console test target needs no wavetable bank. Message-thread data only: it
// describes and constructs, it never dispatches (rendering stays virtual on Layer).
namespace mu_toni
{

// The factory wavetables every Arp built through arpLayerTypeInfo() reads: one process-wide, read-only bank,
// built on first use (message thread) and outliving every layer, tail and processor. A stand-in for the
// per-processor layer services of design 2.4; mu-Toni's own processor keeps its own member bank.
inline const mu_wavetable::WavetableBank& sharedArpBank()
{
    struct Holder
    {
        mu_wavetable::WavetableBank bank;
        Holder() { bank.loadFactoryBank(); }
    };
    static const Holder holder;
    return holder.bank;
}

// A fresh Arp for `slot`, not yet bound or prepared: its voice reads the shared bank, and its noise is seeded
// from a fresh salt and the slot, so no two layers share a noise sequence (the salt is 0 in deterministic mode).
inline std::unique_ptr<Layer> createArp(int slot, mu_core::LayerTypeId /*type*/)
{
    auto a = std::make_unique<Arp>();
    a->runner.setBank(&sharedArpBank());
    a->runner.setSlotIndex(slot, mu_core::makeNoiseSalt());
    a->insert->setSlotIndex(slot);
    return a;
}

inline mu_core::LayerTypeInfo arpLayerTypeInfo() noexcept
{
    mu_core::LayerTypeInfo info;
    info.id = mu_core::LayerTypeId::Arp;
    info.name = "Arp";
    info.group = "mu-Toni";
    info.paramPrefixLetter = 'a';
    info.presetExtension = ".muArp";
    info.contentFolderName = "muToni";   // the layer presets are mu-Toni's own, shared with it, not copied
    info.presetFolder = "Arps";
    info.addLayerParams = &addLayerParams;
    info.create = &createArp;
    return info;
}

} // namespace mu_toni
