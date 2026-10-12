#pragma once

#include "Sequencer/Layer.h"
#include "Sequencer/EuclideanGenerator.h"
#include "Sequencer/HitGenerator.h"

#include <atomic>
#include <optional>

namespace mu_core {

// An int the audio thread reads while the message thread writes it, with the copy a layer needs
// (an atomic alone is not copyable). Relaxed: each value stands on its own, nothing is ordered by it.
struct RelaxedInt
{
    std::atomic<int> v;

    RelaxedInt(int x = 0) noexcept : v(x) {}
    RelaxedInt(const RelaxedInt& o) noexcept : v(o.load()) {}
    RelaxedInt& operator=(const RelaxedInt& o) noexcept { store(o.load()); return *this; }

    int  load() const noexcept { return v.load(std::memory_order_relaxed); }
    void store(int x) noexcept { v.store(x, std::memory_order_relaxed); }
};

// What a rhythm's hits do: play the loaded sample, or send a MIDI note.
enum class OutputMode { Sample = 0, Midi = 1 };

enum class Logic { OR, AND, XOR, AOnly, BOnly };

// One layer's bundle of modulated euclid overrides for genA / genB / genC.
struct EuclidOverrides
{
    EuclidGenOverrides a, b, c;
    bool operator==(const EuclidOverrides& o) const noexcept
    { return a == o.a && b == o.b && c == o.c; }
    bool operator!=(const EuclidOverrides& o) const noexcept { return !(*this == o); }
};

class Rhythm : public Layer
{
public:
    HitGenerator        genA;
    HitGenerator        genB;
    HitGenerator        genC;  // accent pattern (full controls: steps/hits/rot + pads + insert)
    Logic               logic       = Logic::OR;
    std::optional<int>  resetSteps;   // nullopt = INF (free-running)
    // pattern-aware legato. When true, the sequencer marks adjacent hits
    // (pattern[s] && pattern[s-1]) as "tied"; tied steps retrigger the sample
    // voice but skip the envelope noteOn so the envelope state continues
    // uninterrupted across the contiguous run. See BlockResult::tiedMask.
    bool                patternLegato = false;

    // Output mode and the MIDI-output settings. Written by the message thread (parameter apply), read by
    // the audio thread at each hit, hence RelaxedInt. Defaults equal the parameters' defaults.
    RelaxedInt          outputMode   { (int) OutputMode::Sample };
    RelaxedInt          midiNote     { 36 };    // 0..127
    RelaxedInt          midiChannel  { 1 };     // 1..16
    RelaxedInt          midiVelocity { 100 };   // 1..100 %
    RelaxedInt          midiAccent   { 0 };     // 0..100 %, as mu-Clid MIDI's Accent

    // Returns the combined hit pattern for one full cycle.
    // Length is resetSteps if set, otherwise the LCM of A and B step counts.
    std::vector<bool> getCombinedPattern() const;

    // Non-allocating override-aware variant. Writes combined pattern
    // into `out`, using three scratches (genA pattern, genB pattern, plus internal
    // euclidean scratch shared by the HitGenerator overload). All buffers must be
    // pre-reserved to capacity ≥ 256 for fully allocation-free operation.
    void getCombinedPattern(const EuclidOverrides& ov,
                            std::vector<bool>& out,
                            std::vector<bool>& patA,
                            std::vector<bool>& patB,
                            std::vector<bool>& euclidScratch) const;
};

} // namespace mu_core
