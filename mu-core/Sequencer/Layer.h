#pragma once

#include "Sequencer/ControlSequence.h"
#include "Modulation/ModulationMatrix.h"
#include "Audio/VoiceParams.h"
#include "Sequencer/ClipBank.h"
#include "Sequencer/RenderContext.h"
#include "Sequencer/RetireTail.h"
#include "MuLimits.h"

#include <juce_data_structures/juce_data_structures.h>
#include <juce_audio_basics/juce_audio_basics.h>

namespace juce { class AudioProcessorValueTreeState; }

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
//
// A Layer is also a mu_core::RetireTail (retirePolicy, beginRetire, onRetireStart, renderRetiring), so the
// shared RetireBin can hold a whole layer that keeps sounding after it left its slot. It stays a copyable
// value: heavy members (engines, inserts) are SlotBound / unique_ptr members a copy leaves alone.
struct Layer : public mu_core::RetireTail
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

    // The layer's stored clips. SlotBound so a layer-preset load, a clip launch and adoptDefaults never
    // overwrite the bank; it still travels with the layer when the layer pointer moves.
    SlotBound<mu_core::ClipBank> clips;

    // The clip this layer was playing when its state was last saved or loaded (-1 = none). Only the
    // composed-state save and load read and write it; the performance state that tracks the playing
    // clip owns the live value. A plain member, so it follows the layer on a move. A layer-preset load
    // does not reset it by itself: ProcessorBase's non-clip stage choke point clears it, and mu-Clid's
    // Rhythm install carries the slot's value across so a clip launch keeps it.
    int activeClip = -1;

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

    // ─── The render contract (docs/mu-tronic/design.md 2.1) ─────────────────────────────────────
    // A host that renders through ProcessorBase::renderAndMix calls these; every default does nothing, so a
    // product that has not adopted the contract is unchanged. All are called once per layer per block or per
    // edit, never per sample.
    //
    // The type's id, a mu_core::LayerTypeId value (Sequencer/LayerTypeInfo.h). Any thread. The base Layer is
    // Empty, which renderLayers skips (its slot's scratch is cleared instead).
    virtual int typeId() const { return 0; }
    // Message thread, while the audio thread cannot reach the layer (not yet published, or not processing):
    // allocate everything render needs (engine, buffers, insert) for this rate and block size.
    virtual void prepare(double sampleRate, int blockSize) { juce::ignoreUnused(sampleRate, blockSize); }
    // Message thread, before publication and again after every move to another slot (C9): cache the slot's
    // parameter atomics (mu_core::ParamBinding). render never looks a parameter up by id.
    virtual void bindParams(juce::AudioProcessorValueTreeState& apvts, int slot) { juce::ignoreUnused(apvts, slot); }
    // Audio thread: frames [0, n) of `out` (two channels, handed over cleared) become the layer's block,
    // insert included. No allocation, no lock beyond the layer's own spin locks, no parameter lookup by id,
    // no call into the host: the only outputs are `out`, ctx.midiOut and the layer's own atomics.
    // A playing block may REPEAT the previous block's startBeat: a host that stalls its position while reporting
    // play (a freeze) is not a jump, so a transport-anchored layer keeps a last-evaluated-beat cursor and fires
    // nothing it already fired; the one discontinuity arrives as onTransportJump when the position moves again.
    virtual void render(const mu_core::RenderContext& ctx, juce::AudioBuffer<float>& out, int n) { juce::ignoreUnused(ctx, out, n); }
    // 0 renders in the first pass, 1 after every priority-0 layer (a layer that reads a peer through ctx.peer).
    virtual int renderPriority() const { return 0; }
    // Audio thread: the loop length in beats the hot-swap boundary uses; cached per slot by the base each
    // rendered block. Negative: the slot has no loop to wait for. Default: the 4/4 bar (the swapper's common rule).
    virtual double loopBeats() const { return 4.0; }
    // Message thread: file work for a staged layer-preset tree (a sample, a user wavetable), so the commit at
    // the boundary does none.
    virtual void preloadStaged(const juce::ValueTree& staged) { juce::ignoreUnused(staged); }
    // Audio thread, before any render of the block that carries the edge: the play-to-stop edge (silence held
    // notes and gates) and a discontinuity while playing (locate, host loop, clock snap: release or re-anchor
    // pending notes). A type acts on these hooks or on the same flags inside render, not on both.
    virtual void onTransportStopped(const mu_core::RenderContext& ctx) { juce::ignoreUnused(ctx); }
    virtual void onTransportJump(const mu_core::RenderContext& ctx)    { juce::ignoreUnused(ctx); }
    // Audio thread, once per call for a layer parked in a slot at or above the live count (a spare a product
    // keeps): it renders nothing and is not mixed, but it may follow the call's MIDI and flags so input state
    // it keeps (the keys held down) is current when the slot goes live again. Nothing else: no audio, no
    // output, no parameter. A layer that finds it was parked in the last rendered block also knows it has just
    // gone live (a slot that starts from silence).
    virtual void onParkedBlock(const mu_core::RenderContext& ctx)      { juce::ignoreUnused(ctx); }

    // The insert effect's gain-reduction meter (0..1, written by the audio thread, read by the UI), or
    // null when the layer has no insert or the algorithm shows none. Message thread only: the pointer
    // is into this layer, so a caller drops it before the layer is removed.
    virtual const std::atomic<float>* insertGainReduction() const { return nullptr; }
};

// The base persistence hooks need the modulator (de)serialiser, which itself takes a Layer.
#include "Modulation/ModulatorSerialise.h"
