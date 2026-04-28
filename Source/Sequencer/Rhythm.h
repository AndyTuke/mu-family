#pragma once

#include "HitGenerator.h"
#include <optional>
#include <string>

enum class Logic { OR, AND, XOR, AOnly, BOnly };

class Rhythm
{
public:
    HitGenerator        genA;
    HitGenerator        genB;
    Logic               logic       = Logic::OR;
    std::string         name        = "Rhythm";
    int                 colourIndex = 0;     // index into the 30-colour palette
    std::optional<int>  resetSteps;          // nullopt = INF (free-running)

    // Returns the combined hit pattern for one full cycle.
    // Length is resetSteps if set, otherwise the LCM of A and B step counts.
    std::vector<bool> getCombinedPattern() const;
};
