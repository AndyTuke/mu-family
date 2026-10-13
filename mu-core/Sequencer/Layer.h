#pragma once

#include "Sequencer/ControlSequence.h"
#include "Modulation/ModulationMatrix.h"
#include "Audio/VoiceParams.h"
#include "MuLimits.h"

#include <juce_data_structures/juce_data_structures.h>

#include <atomic>
#include <string>
#include <utility>
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

// A non-owning pointer to the owning processor that a layer copy never carries over: assigning one
// layer over another (remove / swap / reset) must keep the destination's own link.
template <class T>
struct OwnerLink
{
    T* ptr = nullptr;

    OwnerLink() = default;
    OwnerLink(const OwnerLink&) noexcept {}
    OwnerLink& operator=(const OwnerLink&) noexcept { return *this; }
};

// A member that belongs to the layer's SLOT (its running DSP state: an insert effect and its delay
// lines), not to the layer's data. Copying or assigning a layer over another leaves the destination's
// own member untouched, so nothing heavy is copied or allocated; a copy starts with a default one.
template <class T>
struct SlotBound
{
    T value {};

    SlotBound() = default;
    SlotBound(const SlotBound&) {}
    SlotBound& operator=(const SlotBound&) noexcept { return *this; }

    T*       operator->() noexcept       { return &value; }
    const T* operator->() const noexcept { return &value; }
    T&       operator*() noexcept        { return value; }
    const T& operator*() const noexcept  { return value; }
};

// Base struct shared by all mu-family voice slots.
// Rhythm (mu-clid) extends this with Euclidean hit generators.
// Future mu-tant voice types extend it with their own trigger data.
struct Layer
{
    static constexpr int MaxControlSequences = mu_limits::kMaxControlSequences;

    virtual ~Layer() = default;
    Layer(const Layer&)            = default;
    Layer& operator=(const Layer&) = default;
    Layer(Layer&&)                 = default;
    Layer& operator=(Layer&&)      = default;

    Layer()
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
    // (via syncRhythmParam / forceSyncRhythmFromApvts / preset apply) and
    // audio-thread reads (the modulation-seed `VoiceParams modParams = ...`
    // copy in processBlock). Without this the struct copy could interleave
    // word-aligned scalar reads with concurrent writes; benign on x86 by
    // accident but UB at the C++ level. Held for nanoseconds either side,
    // so contention is essentially zero.
    mutable CopyableSpinLock voiceParamsLock;

    std::string name        = "<unnamed>";
    int         colourIndex = 0;   // index into MuLookAndFeel::layerPalette (8 colours)

    // Persistence hooks (message thread; defined in Modulation/ModulatorSerialise.h). `writeExtras`
    // adds the layer's non-parameter data to its slot node; `applyExtras` restores it, clearing
    // whatever an absent child stands for. The base handles the modulators, gated by `isValidDest`;
    // a product overrides to add its own data (and calls the base first).
    virtual void writeExtras(juce::ValueTree& node) const;
    virtual void applyExtras(const juce::ValueTree& node);
    virtual bool isValidDest(const std::string& id) const { juce::ignoreUnused(id); return true; }

    // Structural-edit hooks, called by ProcessorBase::addLayer / removeLayer / swapLayers /
    // resetLayer on the message thread with layersLock held, so each must stay short, never block and
    // never allocate. adoptDefaults takes the data of `fresh` (a default layer the caller built before
    // locking) into this layer by swap or plain copy, keeping this layer's identity (name, colour and
    // address); whatever `fresh` is left holding is destroyed by the caller after the lock is dropped.
    // A product extends it to take its own data. onMoved tells a layer its slot index changed, so
    // state tied to the slot (not the data) can be reset.
    virtual void adoptDefaults(Layer& fresh)
    {
        voiceParams = fresh.voiceParams;
        // Exchange the sequences one by one, not the vector: an editor holds pointers to these
        // elements, and a vector swap would leave them pointing into `fresh`, which dies after unlock.
        for (size_t i = 0; i < controlSequences.size() && i < fresh.controlSequences.size(); ++i)
            std::swap(controlSequences[i], fresh.controlSequences[i]);
        std::swap(modulationMatrix, fresh.modulationMatrix);
    }
    virtual void onMoved(int newIndex) { juce::ignoreUnused(newIndex); }

    // The insert effect's gain-reduction meter (0..1, written by the audio thread, read by the UI), or
    // null when the layer has no insert or the algorithm shows none. Message thread only: the pointer
    // is into this layer, so a caller drops it before the layer is removed.
    virtual const std::atomic<float>* insertGainReduction() const { return nullptr; }
};

// The base persistence hooks need the modulator (de)serialiser, which itself takes a Layer.
#include "Modulation/ModulatorSerialise.h"
