#pragma once

#include "VoiceSlot.h"
#include "HitGenerator.h"

#include <optional>

enum class Logic { OR, AND, XOR, AOnly, BOnly };

// #336: per-rhythm bundle of modulated euclid overrides for genA / genB / genC.
struct EuclidOverrides
{
    EuclidGenOverrides a, b, c;
    bool operator==(const EuclidOverrides& o) const noexcept
    { return a == o.a && b == o.b && c == o.c; }
    bool operator!=(const EuclidOverrides& o) const noexcept { return !(*this == o); }
};

class Rhythm : public VoiceSlot
{
public:
    Rhythm() : VoiceSlot() {}

    // std::atomic<bool> in VoiceSlot is non-copyable — delegate to VoiceSlot's copy ops.
    Rhythm(const Rhythm& other)
        : VoiceSlot(other),
          genA(other.genA), genB(other.genB), genC(other.genC),
          logic(other.logic),
          resetSteps(other.resetSteps),
          patternLegato(other.patternLegato)
    {}

    Rhythm& operator=(const Rhythm& other)
    {
        if (this != &other)
        {
            VoiceSlot::operator=(other);
            genA          = other.genA;
            genB          = other.genB;
            genC          = other.genC;
            logic         = other.logic;
            resetSteps    = other.resetSteps;
            patternLegato = other.patternLegato;
        }
        return *this;
    }

    HitGenerator        genA;
    HitGenerator        genB;
    HitGenerator        genC;  // accent pattern (full controls: steps/hits/rot + pads + insert)
    Logic               logic       = Logic::OR;
    std::optional<int>  resetSteps;   // nullopt = INF (free-running)
    // #419: pattern-aware legato. When true, the sequencer marks adjacent hits
    // (pattern[s] && pattern[s-1]) as "tied"; tied steps retrigger the sample
    // voice but skip the envelope noteOn so the envelope state continues
    // uninterrupted across the contiguous run. See BlockResult::tiedMask.
    bool                patternLegato = false;

    // Returns the combined hit pattern for one full cycle.
    // Length is resetSteps if set, otherwise the LCM of A and B step counts.
    std::vector<bool> getCombinedPattern() const;

    // #336 Stage B: non-allocating override-aware variant. Writes combined pattern
    // into `out`, using three scratches (genA pattern, genB pattern, plus internal
    // euclidean scratch shared by the HitGenerator overload). All buffers must be
    // pre-reserved to capacity ≥ 256 for fully allocation-free operation.
    void getCombinedPattern(const EuclidOverrides& ov,
                            std::vector<bool>& out,
                            std::vector<bool>& patA,
                            std::vector<bool>& patB,
                            std::vector<bool>& euclidScratch) const;
};
