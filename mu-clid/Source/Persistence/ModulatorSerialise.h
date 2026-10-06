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

// Modulator data saved before the family depth standard measured Pre Pad / Post Pad depth
// against 12 steps; the standard measures it against the knobs' full 0..63 range. Rescale
// those assignments so an old preset modulates by the same number of steps. Every other
// mu-Clid target's old units already equalled its knob range, so it loads unchanged.
inline juce::ValueTree upgradeModDepthsToRangeStandard(const juce::ValueTree& mods)
{
    if (! mods.isValid() || mods.getProperty(kDepthUnitsProperty).toString() == kDepthUnitsRange)
        return mods;

    constexpr float kOldPadSteps = 12.0f, kPadRangeSteps = 63.0f;
    auto upgraded = mods.createCopy();
    for (int i = 0; i < upgraded.getNumChildren(); ++i)
    {
        auto node = upgraded.getChild(i);
        const auto dest = node.getProperty("dest").toString();
        if (dest.startsWith("euclid.") && (dest.endsWith(".prePad") || dest.endsWith(".postPad")))
            node.setProperty("depth", (double) node.getProperty("depth") * kOldPadSteps / kPadRangeSteps, nullptr);
    }
    upgraded.setProperty(kDepthUnitsProperty, kDepthUnitsRange, nullptr);
    return upgraded;
}

inline juce::StringArray deserialiseModulators(const juce::ValueTree& mods, Rhythm& r)
{
    return deserialiseModulators(upgradeModDepthsToRangeStandard(mods), static_cast<VoiceSlot&>(r),
        [](const std::string& id) { return ModDest::isValidSourceId(id); },
        [](const std::string& id) { return ModDest::isValidDestinationId(id); });
}

inline void clearModulators(Rhythm& r)
{
    clearModulators(static_cast<VoiceSlot&>(r));
}

} // namespace mu_pp
