#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Persistence/LayerState.h"   // kGlobalsTag, kRowTag, readRowValue
#include <cmath>

// States saved before the reverb had a Decay knob carry no rev_decay. Their reverb decayed by the
// algorithm's own time, so a load gives them the Decay that sounds as it did then, instead of the
// 2.5 s default. A state that saves rev_decay (any value, 2.5 included) is never touched.
namespace mu_pp
{

inline constexpr const char* kReverbDecayId = "rev_decay";
inline constexpr const char* kReverbAlgoId  = "rev_algo";

// The Decay (RT60 seconds) that reproduces the old tail of reverb algorithm `algorithm` (0 Room,
// 1 Hall, 2 Plate, 3 Spring). The slot used to take the algorithm's table value as the library's
// rt20 (0.6f, 2.5f, 1.8f, 0.35f); it now takes decay / 3. Each constant is the float that, after the
// parameter's 0..1 round trip, divides by 3 closest to the old rt20: Room and Hall exactly, Plate
// and Spring within 8e-8 s (no float divides by 3 to 1.8f or 0.35f exactly).
inline float legacyReverbDecay(int algorithm) noexcept
{
    static constexpr float kDecay[] = { 0x1.cccccep+0f,  // Room   (0.6f  * 3.0f, exact)
                                        0x1.ep+2f,          // Hall   (7.5, exact)
                                        0x1.599998p+2f,     // Plate  (1.8f  * 3.0f, rt20 short by 7.9e-8)
                                        0x1.0ccccep+0f };   // Spring (one ulp above 0.35f * 3.0f, rt20 long by 3.0e-8)
    return kDecay[juce::jlimit(0, 3, algorithm)];
}

// A composed state whose <Globals> has no rev_decay row, as a copy with the row its rev_algo row
// implies (the parameter's default when that row is missing too, as the apply would load it). Any
// other state, a state without <Globals> included, comes back as it is.
inline juce::ValueTree withLegacyReverbDecay(const juce::ValueTree& state, juce::RangedAudioParameter* revAlgo)
{
    const auto globals = state.getChildWithName(kGlobalsTag);
    if (! globals.isValid() || revAlgo == nullptr) return state;

    // Find the decay and algorithm rows: a saved decay ends the migration.
    float algoNormalised = revAlgo->getDefaultValue();
    for (int i = 0; i < globals.getNumChildren(); ++i)
    {
        const auto row = globals.getChild(i);
        if (! row.hasType(kRowTag)) continue;
        const auto id = row.getProperty("id").toString();
        if (id == kReverbDecayId) return state;
        if (id == kReverbAlgoId) readRowValue(row, *revAlgo, algoNormalised);
    }

    auto migrated = state.createCopy();
    juce::ValueTree row(kRowTag);
    row.setProperty("id", kReverbDecayId, nullptr);
    row.setProperty("x", legacyReverbDecay(juce::roundToInt(revAlgo->convertFrom0to1(algoNormalised))), nullptr);
    migrated.getChildWithName(kGlobalsTag).appendChild(row, nullptr);
    return migrated;
}

// The algorithm `revAlgo` holds now.
inline int currentReverbAlgorithm(const juce::RangedAudioParameter& revAlgo)
{
    return juce::roundToInt(revAlgo.convertFrom0to1(revAlgo.getValue()));
}

// The same for an APVTS dump (<PARAM id value> children, actual values), in place. replaceState
// gives a parameter with no PARAM, or a PARAM without a value, its default (the PARAM it adds for
// the parameter has no value), so such a dump loads the default algorithm.
inline void addLegacyReverbDecayParam(juce::ValueTree& dump, juce::RangedAudioParameter* revAlgo)
{
    if (revAlgo == nullptr || dump.getChildWithProperty("id", kReverbDecayId).isValid()) return;

    int algorithm = juce::roundToInt(revAlgo->convertFrom0to1(revAlgo->getDefaultValue()));
    if (const auto algo = dump.getChildWithProperty("id", kReverbAlgoId); algo.isValid() && algo.hasProperty("value"))
        algorithm = juce::roundToInt((double) algo.getProperty("value"));

    juce::ValueTree param("PARAM");
    param.setProperty("id", kReverbDecayId, nullptr);
    param.setProperty("value", legacyReverbDecay(algorithm), nullptr);
    dump.appendChild(param, nullptr);
}

} // namespace mu_pp
