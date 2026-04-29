#pragma once

#include "HitGenerator.h"
#include "ControlSequence.h"
#include "../Modulation/ModulationMatrix.h"

#include <optional>
#include <string>
#include <vector>

enum class Logic { OR, AND, XOR, AOnly, BOnly };

class Rhythm
{
public:
    static constexpr int MaxControlSequences = 8;

    Rhythm()
    {
        controlSequences.resize(MaxControlSequences);
        for (int i = 0; i < MaxControlSequences; ++i)
            controlSequences[i].id = "cs" + std::to_string(i);
    }

    HitGenerator        genA;
    HitGenerator        genB;
    Logic               logic       = Logic::OR;
    std::string         name        = "Rhythm";
    int                 colourIndex = 0;     // index into the 30-colour palette
    std::optional<int>  resetSteps;          // nullopt = INF (free-running)

    std::vector<ControlSequence> controlSequences;
    ModulationMatrix             modulationMatrix;

    // Returns the combined hit pattern for one full cycle.
    // Length is resetSteps if set, otherwise the LCM of A and B step counts.
    std::vector<bool> getCombinedPattern() const;
};
