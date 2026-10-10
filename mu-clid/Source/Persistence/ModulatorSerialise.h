#pragma once

#include "Modulation/ModulatorSerialise.h"      // mu-core generic (Layer-based, namespace mu_pp)
#include "Sequencer/Rhythm.h"
#include "Modulation/ModulationDestinations.h"

// μ-Clid adapter over the shared mu-core modulator (de)serialise. The generic
// implementation lives in mu-core and operates on the Layer base; these
// thin Rhythm& forwarders inject μ-Clid's ModDest source/destination validators
// so existing call sites + tests (which pass `Rhythm`) are unchanged. enumName /
// readEnumIndex stay in mu-core's mu_pp (the Layer-based serialise there is what these forward to).

namespace mu_clid {

// Marks mu-Clid modulator data whose Pre / Post Pad and Insert Length depths are measured
// against the knob's current maximum (HitGenerator::padKnobMaxima) rather than the full
// 0..63 / 0..8 parameter range.
inline constexpr const char* kPadDepthUnitsProperty = "padDepthUnits";
inline constexpr const char* kPadDepthUnitsKnobMax  = "knobMax";

inline juce::ValueTree serialiseModulators(const Rhythm& r)
{
    auto mods = mu_pp::serialiseModulators(static_cast<const Layer&>(r));
    mods.setProperty(kPadDepthUnitsProperty, kPadDepthUnitsKnobMax, nullptr);
    return mods;
}

// Modulator data saved before the family depth standard measured Pre Pad / Post Pad depth
// against 12 steps; the standard measures it against the knobs' full 0..63 range. Rescale
// those assignments so an old preset modulates by the same number of steps. Every other
// mu-Clid target's old units already equalled its knob range, so it loads unchanged.
inline juce::ValueTree upgradeModDepthsToRangeStandard(const juce::ValueTree& mods)
{
    if (! mods.isValid() || mods.getProperty(mu_pp::kDepthUnitsProperty).toString() == mu_pp::kDepthUnitsRange)
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
    upgraded.setProperty(mu_pp::kDepthUnitsProperty, mu_pp::kDepthUnitsRange, nullptr);
    return upgraded;
}

// Modulator data saved before pad depths followed the knob's current maximum measured them
// against the full parameter range (0..63 steps for the pads, 0..8 for Insert Length).
// Rescale by fullRange / currentMax — from `r`'s already-loaded Euclid layout — so an old
// preset modulates by the same number of steps; clamped to ±100%, which was as far as the
// old depth could push the pattern anyway. A knob with no room (max 0) keeps its depth.
inline juce::ValueTree upgradePadDepthsToKnobMax(const juce::ValueTree& mods, const Rhythm& r)
{
    if (! mods.isValid() || mods.getProperty(kPadDepthUnitsProperty).toString() == kPadDepthUnitsKnobMax)
        return mods;

    auto upgraded = mods.createCopy();
    for (int i = 0; i < upgraded.getNumChildren(); ++i)
    {
        auto node = upgraded.getChild(i);
        const auto dest = node.getProperty("dest").toString();
        if (! dest.startsWith("euclid.")) continue;

        // Ring letter → generator; target suffix → full range + current knob maximum.
        const juce::juce_wchar ring = dest[7];
        const HitGenerator* gen = ring == 'a' ? &r.genA : ring == 'b' ? &r.genB : ring == 'c' ? &r.genC : nullptr;
        if (gen == nullptr) continue;
        const auto max = gen->padKnobMaxima();
        int fullRange = 0, knobMax = 0;
        if      (dest.endsWith(".prePad"))  { fullRange = HitGenerator::kMaxPrePad;       knobMax = max.prePad; }
        else if (dest.endsWith(".postPad")) { fullRange = HitGenerator::kMaxPostPad;      knobMax = max.postPad; }
        else if (dest.endsWith(".insLen"))  { fullRange = HitGenerator::kMaxInsertLength; knobMax = max.insertLength; }
        if (fullRange == 0 || knobMax == 0) continue;

        const double depth = (double) node.getProperty("depth") * fullRange / knobMax;
        node.setProperty("depth", juce::jlimit(-100.0, 100.0, depth), nullptr);
    }
    upgraded.setProperty(kPadDepthUnitsProperty, kPadDepthUnitsKnobMax, nullptr);
    return upgraded;
}

// Every load path calls this after the rhythm's Euclid values are in place, so the
// pad-depth upgrade sees the layout the preset plays with.
inline juce::StringArray deserialiseModulators(const juce::ValueTree& mods, Rhythm& r)
{
    return mu_pp::deserialiseModulators(upgradePadDepthsToKnobMax(upgradeModDepthsToRangeStandard(mods), r),
                                 static_cast<Layer&>(r),
        [](const std::string& id) { return ModDest::isValidSourceId(id); },
        [](const std::string& id) { return ModDest::isValidDestinationId(id); });
}

inline void clearModulators(Rhythm& r)
{
    mu_pp::clearModulators(static_cast<Layer&>(r));
}

} // namespace mu_clid
