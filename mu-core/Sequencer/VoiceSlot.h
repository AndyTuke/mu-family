#pragma once

#include "Sequencer/ControlSequence.h"
#include "Modulation/ModulationMatrix.h"
#include "Audio/VoiceParams.h"
#include "MuLimits.h"

#include <atomic>
#include <string>
#include <vector>

// Spin-lock backed by std::atomic<bool> with copy-safe semantics.
// Copies always produce a new, unlocked instance — the lock state is never
// transferred, since each copy is an independent object.
// Forwarding the atomic interface lets call sites use .exchange/.store/.compare_exchange_strong
// directly; take the underlying atomic via .v when a std::atomic<bool>* is needed.
struct CopyableSpinLock
{
    mutable std::atomic<bool> v { false };

    CopyableSpinLock() = default;
    CopyableSpinLock(const CopyableSpinLock&) noexcept {}
    CopyableSpinLock& operator=(const CopyableSpinLock&) noexcept { return *this; }

    bool exchange(bool val, std::memory_order mo) const noexcept
        { return v.exchange(val, mo); }
    void store(bool val, std::memory_order mo) const noexcept
        { v.store(val, mo); }
    bool compare_exchange_strong(bool& expected, bool desired, std::memory_order mo) const noexcept
        { return v.compare_exchange_strong(expected, desired, mo); }
};

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

    VoiceParams                   voiceParams;
    std::vector<ControlSequence>  controlSequences;
    ModulationMatrix              modulationMatrix;

    // Spin-lock protecting modulationMatrix and controlSequences from concurrent
    // message-thread writes and audio-thread reads.
    mutable CopyableSpinLock modLock;
    // Spin-lock protecting `voiceParams` from concurrent message-thread writes
    // (via syncRhythmParam / forceSyncRhythmFromAPVTS / preset apply) and
    // audio-thread reads (the modulation-seed `VoiceParams modParams = ...`
    // copy in processBlock). Without this the struct copy could interleave
    // word-aligned scalar reads with concurrent writes; benign on x86 by
    // accident but UB at the C++ level. Held for nanoseconds either side,
    // so contention is essentially zero.
    mutable CopyableSpinLock voiceParamsLock;

    std::string name        = "<unnamed>";
    int         colourIndex = 0;   // index into MuLookAndFeel::channelPalette (8 colours; currently `rhythmPalette` pending #662 rename)
};
