#pragma once

#include "VoiceSlot.h"
#include "HitGenerator.h"

#include <optional>

enum class Logic { OR, AND, XOR, AOnly, BOnly };

class Rhythm : public VoiceSlot
{
public:
    Rhythm() : VoiceSlot() {}

    // std::atomic<bool> in VoiceSlot is non-copyable — delegate to VoiceSlot's copy ops.
    Rhythm(const Rhythm& other)
        : VoiceSlot(other),
          genA(other.genA), genB(other.genB), genC(other.genC),
          logic(other.logic),
          resetSteps(other.resetSteps)
    {}

    Rhythm& operator=(const Rhythm& other)
    {
        if (this != &other)
        {
            VoiceSlot::operator=(other);
            genA       = other.genA;
            genB       = other.genB;
            genC       = other.genC;
            logic      = other.logic;
            resetSteps = other.resetSteps;
        }
        return *this;
    }

    HitGenerator        genA;
    HitGenerator        genB;
    HitGenerator        genC;  // accent pattern (full controls: steps/hits/rot + pads + insert)
    Logic               logic       = Logic::OR;
    std::optional<int>  resetSteps;   // nullopt = INF (free-running)

    // Returns the combined hit pattern for one full cycle.
    // Length is resetSteps if set, otherwise the LCM of A and B step counts.
    std::vector<bool> getCombinedPattern() const;
};
