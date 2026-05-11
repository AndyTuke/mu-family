#pragma once

#include <string>

struct ModulationAssignment
{
    std::string id;              // unique stable ID for this assignment
    std::string sourceId;        // "cs{n}_output" or future "assign_{id}_depth"
    std::string destinationId;   // parameter ID, e.g. "amplitude_attack"
    float       depth = 0.0f;    // modulation depth, -100..+100
    // #224 Bitwig-style curve knob: bipolar bend applied to the source value before
    // depth scaling. 0 = linear; +100 = exponential (compress low, expand high,
    // exponent 2); −100 = logarithmic (expand low, compress high, exponent 0.5).
    // Sign-preserving for bipolar CS outputs.
    float       curve = 0.0f;    // -100..+100
};
