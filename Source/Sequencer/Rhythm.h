#pragma once

#include "HitGenerator.h"
#include "ControlSequence.h"
#include "../Modulation/ModulationMatrix.h"
#include "../Audio/VoiceParams.h"

#include <atomic>
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

    // std::atomic<bool> is non-copyable, so we provide explicit copy operations.
    // The lock is not copied — copies always start unlocked.
    Rhythm(const Rhythm& other)
        : genA(other.genA), genB(other.genB), genC(other.genC),
          logic(other.logic), name(other.name), colourIndex(other.colourIndex),
          resetSteps(other.resetSteps), voiceParams(other.voiceParams),
          midiMode(other.midiMode), controlSequences(other.controlSequences),
          modulationMatrix(other.modulationMatrix)
    {}

    Rhythm& operator=(const Rhythm& other)
    {
        if (this != &other)
        {
            genA             = other.genA;
            genB             = other.genB;
            genC             = other.genC;
            logic            = other.logic;
            name             = other.name;
            colourIndex      = other.colourIndex;
            resetSteps       = other.resetSteps;
            voiceParams      = other.voiceParams;
            midiMode         = other.midiMode;
            controlSequences = other.controlSequences;
            modulationMatrix = other.modulationMatrix;
            // modLock is intentionally not copied — target lock stays in its current state.
        }
        return *this;
    }

    HitGenerator        genA;
    HitGenerator        genB;
    HitGenerator        genC;  // accent pattern (full controls: steps/hits/rot + pads + insert)
    Logic               logic       = Logic::OR;
    std::string         name        = "Rhythm";
    int                 colourIndex = 0;     // index into the 30-colour palette
    std::optional<int>  resetSteps;          // nullopt = INF (free-running)

    VoiceParams voiceParams;
    bool        midiMode    = false;

    std::vector<ControlSequence> controlSequences;
    ModulationMatrix             modulationMatrix;

    // Spin-lock protecting modulationMatrix and controlSequences from concurrent
    // message-thread writes and audio-thread reads.  Audio thread uses try-lock;
    // message thread spins until acquired.
    mutable std::atomic<bool> modLock { false };

    // Returns the combined hit pattern for one full cycle.
    // Length is resetSteps if set, otherwise the LCM of A and B step counts.
    std::vector<bool> getCombinedPattern() const;
};
