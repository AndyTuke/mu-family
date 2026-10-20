#pragma once

#include "Sequencer/Arp.h"
#include "Sequencer/LayerTypeInfo.h"        // mu-core: mu_core::LayerTypeInfo
#include "Sequencer/LayerServices.h"        // mu-core: what the type shares between its layers
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

// What every Arp of one processor shares (design 2.4): the factory wavetables, read-only, so the audio thread reads
// them with no lock. Built when the processor's first Arp is added (message thread) and kept until the processor is
// destroyed, after every Arp and tail. mu-Toni's own processor keeps its own member bank and does not use this.
class ArpServices : public mu_core::LayerServices
{
public:
    ArpServices() { bank.loadFactoryBank(); }
    mu_wavetable::WavetableBank bank;
};

inline std::unique_ptr<mu_core::LayerServices> createArpServices(ProcessorBase& /*host*/)
{
    return std::make_unique<ArpServices>();
}

// A fresh Arp for `slot`, not yet bound or prepared: its noise is seeded from a fresh salt and the slot, so no two
// layers share a noise sequence (the salt is 0 in deterministic mode). Its voice reads the services' bank once
// attachArpServices has run.
inline std::unique_ptr<Layer> createArp(int slot, mu_core::LayerTypeId /*type*/)
{
    auto a = std::make_unique<Arp>();
    a->runner.setSlotIndex(slot, mu_core::makeNoiseSalt());
    a->insert->setSlotIndex(slot);
    return a;
}

// Points the Arp's voice at the shared factory bank.
inline void attachArpServices(Layer& layer, mu_core::LayerServices& services, int /*slot*/)
{
    static_cast<Arp&>(layer).runner.setBank(&static_cast<ArpServices&>(services).bank);
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
    info.presetTag = kArpPresetTag;
    info.descriptor = "Generative arpeggiator";
    info.addLayerParams = &addLayerParams;
    info.create = &createArp;
    info.createServices = &createArpServices;
    info.attachServices = &attachArpServices;
    return info;
}

} // namespace mu_toni
