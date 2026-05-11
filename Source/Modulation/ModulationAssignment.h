#pragma once

#include <string>

struct ModulationAssignment
{
    std::string id;              // unique stable ID for this assignment
    std::string sourceId;        // "cs{n}_output" or future "assign_{id}_depth"
    std::string destinationId;   // parameter ID, e.g. "amplitude_attack"
    float       depth = 0.0f;    // modulation depth, -100..+100
};
