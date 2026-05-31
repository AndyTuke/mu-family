#pragma once

#include "Modulation/ModulatorSerialise.h"      // mu-core generic (VoiceSlot-based, namespace mu_pp)
#include "Sequencer/Rhythm.h"
#include "Modulation/ModulationDestinations.h"

// μ-Clid adapter over the shared mu-core modulator (de)serialise. The generic
// implementation lives in mu-core and operates on the VoiceSlot base; these
// thin Rhythm& forwarders inject μ-Clid's ModDest source/destination validators
// so existing call sites + tests (which pass `Rhythm`) are unchanged. enumName /
// readEnumIndex are re-exported from mu-core via the include above (mu_pp::).

namespace mu_pp {

inline juce::ValueTree serialiseModulators(const Rhythm& r)
{
    return serialiseModulators(static_cast<const VoiceSlot&>(r));
}

inline juce::StringArray deserialiseModulators(const juce::ValueTree& mods, Rhythm& r)
{
    return deserialiseModulators(mods, static_cast<VoiceSlot&>(r),
        [](const std::string& id) { return ModDest::isValidSourceId(id); },
        [](const std::string& id) { return ModDest::isValidDestinationId(id); });
}

inline void clearModulators(Rhythm& r)
{
    clearModulators(static_cast<VoiceSlot&>(r));
}

} // namespace mu_pp
