#pragma once

#include "ControlSequence.h"
#include "../Modulation/ModulationMatrix.h"
#include "../Audio/VoiceParams.h"
#include "../MuLimits.h"

#include <atomic>
#include <string>
#include <vector>

// Base struct shared by all mu-family voice slots.
// Rhythm (mu-clid) extends this with Euclidean hit generators.
// Future mu-tant voice types extend it with their own trigger data.
struct VoiceSlot
{
    static constexpr int MaxControlSequences = mu_limits::kMaxControlSequences;

    VoiceSlot()
    {
        controlSequences.resize(MaxControlSequences);
        for (int i = 0; i < MaxControlSequences; ++i)
            controlSequences[i].id = "cs" + std::to_string(i);
    }

    // std::atomic<bool> is non-copyable — provide explicit copy operations.
    // The lock is not copied; copies always start unlocked.
    VoiceSlot(const VoiceSlot& other)
        : voiceParams(other.voiceParams),
          controlSequences(other.controlSequences),
          modulationMatrix(other.modulationMatrix),
          name(other.name),
          colourIndex(other.colourIndex)
    {}

    VoiceSlot& operator=(const VoiceSlot& other)
    {
        if (this != &other)
        {
            voiceParams      = other.voiceParams;
            controlSequences = other.controlSequences;
            modulationMatrix = other.modulationMatrix;
            name             = other.name;
            colourIndex      = other.colourIndex;
            // modLock intentionally not copied — target stays in its current state.
        }
        return *this;
    }

    VoiceParams                   voiceParams;
    std::vector<ControlSequence>  controlSequences;
    ModulationMatrix              modulationMatrix;

    // Spin-lock protecting modulationMatrix and controlSequences from concurrent
    // message-thread writes and audio-thread reads.
    mutable std::atomic<bool> modLock { false };

    std::string name        = "<unnamed>";
    int         colourIndex = 0;   // index into the 30-colour palette
};
