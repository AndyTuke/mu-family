#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Sequencer/HitGenerator.h"

namespace mu_core {

// Declares one rhythm's Euclidean sequencing parameters, in the order and with the ids / names / ranges
// every Euclid product shares: the three generators A / B / C (steps, hits, rotate, pads, insert,
// and the pad / insert modes), then the generator logic, pattern legato, voice mono and reset steps.
// `idPrefix` is the parameter id prefix ("r0_"), `namePrefix` the display-name prefix ("Rhythm 1 ").
// One definition so mu-Clid and mu-Clid MIDI cannot drift apart on a range or an id.
inline void addEuclidParams(juce::AudioProcessorValueTreeState::ParameterLayout& layout,
                            const juce::String& idPrefix, const juce::String& namePrefix)
{
    const auto& p = idPrefix;
    const auto& n = namePrefix;

    auto addB = [&](const juce::String& id, const juce::String& name, bool def)
    {
        layout.add(std::make_unique<juce::AudioParameterBool>(id, name, def));
    };
    auto addI = [&](const juce::String& id, const juce::String& name, int mn, int mx, int def)
    {
        layout.add(std::make_unique<juce::AudioParameterInt>(id, name, mn, mx, def));
    };

    // The three generators share one parameter set; `L` is the generator letter.
    for (const char* letter : { "A", "B", "C" })
    {
        const juce::String L(letter);
        addI(p+"steps"+L,   n+"Steps "+L,   1, HitGenerator::kMaxSteps, 8);
        addI(p+"hits"+L,    n+"Hits "+L,    0, 64, 0);
        addI(p+"rot"+L,     n+"Rot "+L,   -32, 32, 0);
        addI(p+"prePad"+L,  n+"PrePad "+L,  0, HitGenerator::kMaxPrePad, 0);
        addI(p+"postPad"+L, n+"PostPad "+L, 0, HitGenerator::kMaxPostPad, 0);
        addI(p+"insSt"+L,   n+"InsSt "+L,   0, 63, 0);
        addI(p+"insLen"+L,  n+"InsLen "+L,  0, HitGenerator::kMaxInsertLength, 0);
        addB(p+"insMode"+L,     n+"InsMode "+L,     false);
        addB(p+"prePadMode"+L,  n+"PrePadMode "+L,  false);
        addB(p+"postPadMode"+L, n+"PostPadMode "+L, false);
    }

    // Logic and the rhythm-level switches.
    addI(p+"logic",  n+"Logic",        0,   4,   0);
    addB(p+"patLeg", n+"Pattern Legato", false);
    addB(p+"vMono",  n+"Voice Mono", false);  // true = polyphony capped at 1 voice
    addI(p+"rstSt",  n+"Reset Steps", -1, 256, -1);  // -1 = free-running (nullopt)
}

} // namespace mu_core
