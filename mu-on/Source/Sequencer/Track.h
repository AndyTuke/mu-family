#pragma once

#include "Sequencer/Layer.h"            // mu-core: per-layer ControlSequences + ModulationMatrix, the render contract
#include "Sequencer/LayerTypeInfo.h"    // mu-core: mu_core::LayerTypeId
#include "Sequencer/RetireTail.h"       // mu-core: mu_core::ParamBinding
#include "Sequencer/StepRow.h"         // the track's steps and groove
#include "Sequencer/StepClock.h"       // the row's step cursor
#include "Audio/TrackEngine.h"          // the engines the track runs
#include "Audio/InsertProcessor.h"      // mu-core: the track's insert effect
#include "Audio/VoiceParams.h"          // mu-core: the insert's parameter snapshot
#include "Modulation/MuOnModDest.h"     // the engines' modulation target tables

#include <array>
#include <atomic>
#include <memory>
#include <string_view>
#include <unordered_map>

namespace mu_on
{

// One mu-On layer, a track. The shared Layer (colour, modulation) plus what is the track's own and moves
// with it through add / delete / reorder: its step row and the row's clock, its Rumble volume envelope, the
// trigger counter the audio thread bumps and the editor polls to pulse the sidebar item, and its sound engines
// (Kick, Bass, Hat, Snare, Rumble, FM Perc; the one that plays is named by its `t{N}_eng` parameter). A track whose engine
// can change (every mu-On track) holds all five, built and prepared, so a change of the parameter from any thread
// (host automation on the audio thread included) switches the engine in the same block, as the per-slot engines
// always did. A track whose host fixes its engine (engineLocked: a mu-Tronic layer) holds only that one.
//
// It renders through the shared layer contract (docs/mu-tronic/design.md 2.1): bindParams caches the slot's
// parameter atomics; render resolves the engine's modulation, clocks the row, triggers and renders the
// engine. A Rumble is a priority-1 layer: it renders after every other track and reads its source track's
// block through RenderContext::peer (post-insert, pre-strip).
//
// Threads. The audio thread runs render / onParkedBlock inside the block's layer access. Engines are built
// and freed only on the message thread (syncEngine), and installed or removed under `engineLock`, a spin lock
// the audio thread try-locks around its use for one call (a miss renders this track silent for that call).
// prepare runs while the host is not processing; bindParams and resetEngineToPrepared run while no block
// can render the track (before publication, during an edit, under layersLock). The UI reads only the
// atomics (modLive, triggers, the row cells).
// The live modulation values sit on their own cache line (alignas), which pads the struct on purpose.
JUCE_BEGIN_IGNORE_WARNINGS_MSVC(4324)
struct Track : Layer
{
    std::atomic<int> triggers { 0 };

    // The track's step row (the pattern points its slot at it) and, when the track runs Rumble, the
    // drawable bar-volume envelope the engine follows. The envelope is edited on the message thread and
    // read by the audio thread under `rumbleEnvLock`; both are kept when the engine changes.
    StepRow           row;
    StepClock         clock;            // the row's step cursor (audio thread)
    ControlSequence   rumbleEnv;
    mutable CopyableSpinLock rumbleEnvLock;

    // The slot this track sits in (kept current by the structural edits through onMoved).
    int slot = 0;

    // True when the host fixes this track's engine (a mu-Tronic layer is created as one of the five types and
    // keeps it): the content hides the Engine selector, engineSetting() returns the locked engine whatever
    // `t{N}_eng` holds, and syncEngine keeps only that engine (one per layer, design 1.4). Set by lockEngine
    // on the message thread before the layer is published; false in mu-On, so the standalone is unchanged.
    // Not saved: the host that locks it sets it when it creates the layer.
    bool engineLocked = false;

    Track();
    ~Track() override;

    // Flat, full level, one bar long: the envelope of a track that has not drawn one.
    void resetRumbleEnv();
    bool rumbleEnvIsDefault() const;

    void writeExtras(juce::ValueTree& node) const override;
    void applyExtras(const juce::ValueTree& node) override;
    bool isValidDest(const std::string& id) const override;
    void onMoved(int newIndex) override { slot = newIndex; }

    // ── The render contract ──────────────────────────────────────────────────
    // The type is the engine the track's parameter names (mu_core::LayerTypeId::Kick .. FmPerc).
    int  typeId() const override;
    // A Rumble renders after every other track (it reads its source's block); everything else first.
    int  renderPriority() const override { return engineSetting() == EngineType::Rumble ? 1 : 0; }
    // Message thread, not processing: the engine's and the track's buffers for this rate and block size. The
    // next rendered block starts the engine from silence (a prepare, like a slot going live, is a fresh start).
    void prepare(double sampleRate, int blockSize) override;
    // Message thread: the slot's t{slot}_ rows (engine, source, Bass wave), the groove rows and every engine's
    // modulation inputs.
    void bindParams(juce::AudioProcessorValueTreeState& apvts, int slot) override;
    // Message thread, once, after the first bindParams: a locked track writes `t{N}_eng` to its engine when it
    // differs (so the saved rows and a saved .muTrack name the right engine and mu-On loads it), then builds
    // the engines it needs (syncEngine).
    void completeSetup(juce::AudioProcessorValueTreeState& apvts, int slot) override;
    // Audio thread: the track's block into out[0, n) (handed over cleared).
    void render(const mu_core::RenderContext& ctx, juce::AudioBuffer<float>& out, int n) override;
    // Audio thread: a parked spare's row clock keeps step with the transport, so it fires on time once live.
    void onParkedBlock(const mu_core::RenderContext& ctx) override;
    // A staged track preset lands on the 16-step row's wrap: the bar.
    double loopBeats() const override { return 4.0; }

    // ── How the track leaves its slot ────────────────────────────────────────
    // Fade (the family defaults, 25 ms and 2 s): the engine that last sounded keeps the parameters it was last
    // given, so the tail copies nothing at beginRetire and never reads a parameter.
    mu_core::RetirePolicy retirePolicy() const override
    {
        mu_core::RetirePolicy p;
        p.kind = mu_core::RetirePolicy::Kind::Fade;
        return p;
    }
    // Audio thread: the engine of the last rendered block rings on with no trigger and no setParams, through
    // the bin's linear fade. A Rumble tail processes the track's silence buffer. False once nothing can sound.
    bool renderRetiring(const mu_core::RenderContext& ctx, juce::AudioBuffer<float>& out, int n, mu_core::RetireState& rs) override;

    // ── The engine ───────────────────────────────────────────────────────────
    // Message thread, before the track is bound or published: fix the track to `engine` (see engineLocked).
    // The audio thread reads the two plain members without a lock, so the layer's publication is what makes them
    // visible: never call this on a track that is already live.
    void lockEngine(EngineType engine) noexcept { engineLocked = true; lockedEngine = engine; }
    // The engine the track runs: the locked one, else the one its `t{N}_eng` parameter names (any thread;
    // Kick before bindParams).
    EngineType engineSetting() const noexcept;
    // Message thread: brings the engines the track holds to the set it needs (all five; with engineLocked only
    // the one its parameter names). Missing engines are built and prepared outside every lock and installed
    // under the engine lock; engines no longer needed are taken out under it and freed here. True when it
    // changed the set. For an unlocked track this runs once (at creation); later parameter changes need none.
    bool syncEngine();
    // Whether the track holds `engine`, built and prepared (any thread).
    bool holdsEngine(EngineType engine) const noexcept
    {
        return ((heldMask.load(std::memory_order_acquire) >> (unsigned) toIndex(engine)) & 1u) != 0;
    }
    // Under layersLock (a structural edit moved this track to another slot): every engine back to its prepared
    // state, as a fresh tenant of that slot. Allocation-free; no block renders the track meanwhile.
    void resetEngineToPrepared() noexcept;

    // Post-matrix value of modulation target `dest` (a row of `engine`'s table), in the target's parameter
    // units, as the last rendered block resolved it; NaN while the track runs another engine (any thread).
    float getModLive(EngineType engine, int dest) const noexcept;

    // The most targets any engine's table holds (Bass: 10).
    static constexpr int kMaxDests = 16;

private:
    // Cached modulation inputs for one engine: the destination ids (proportion-space keys), their backing
    // APVTS atomics and ranges, and the reusable seed/result map (pre-sized in bindParams).
    struct EngineMod
    {
        int count = 0;
        std::array<const char*, kMaxDests>                    ids   {};
        std::array<const std::atomic<float>*, kMaxDests>      atoms {};
        std::array<juce::NormalisableRange<float>, kMaxDests> ranges{};
        std::unordered_map<std::string_view, float>           map;
    };

    void  resolve(int engine, double beat, float* outValues) noexcept;   // modulation, then the modLive publish
    float evaluateRumbleEnv(double beat) noexcept;                       // the bar envelope, under its try-lock
    void  clockRow(const mu_core::RenderContext& ctx, TrackEngine* fireInto, int engine, int n) noexcept;
    void  resetEngines() noexcept;   // every held engine's voice silenced (the per-slot engines' reset rule)

    mu_core::ParamBinding engineRow, sourceRow, bassWaveRow, algoRow, swingRow, accentRow;
    // The Kick / Bass Fine offsets (cents) and the master key and scale their tune is locked to. The key and scale are the
    // host's: a host without them (unbound) leaves the lock at C chromatic.
    mu_core::ParamBinding kickFineRow, bassFineRow, rootRow, scaleRow;
    // The track's insert effect (the shared InsertProcessor, as a mixer bus insert): algorithm and four generic knobs,
    // run on the engine's block after it renders. Algorithm 0 (None) skips it.
    InsertProcessor insert;
    mu_core::ParamBinding insAlgoRow, insParamRow[VoiceParams::kInsertSlotCount];
    std::array<EngineMod, kNumEngineTypes> mods;

    // The UI's live modulation values, on their own cache line (C16): written by the audio thread.
    alignas(64) std::array<std::atomic<float>, kMaxDests> modLive {};
    std::atomic<int> modLiveEngine { -1 };   // the engine those values belong to (-1: none yet)

    // Indexed by EngineType; installed and removed only under engineLock, built and freed only on the message thread.
    std::array<std::unique_ptr<TrackEngine>, kNumEngineTypes> engines;
    mutable CopyableSpinLock     engineLock;
    std::atomic<unsigned>        heldMask { 0 };   // bit e: engines[e] is installed
    juce::CriticalSection        engineBuild; // message side: prepare, syncEngine and resetEngineToPrepared never overlap
    double preparedRate  = 44100.0;
    int    preparedBlock = 512;

    EngineType lockedEngine = EngineType::Kick;   // the engine of a locked track (engineLocked)

    juce::AudioBuffer<float> silence;          // a Rumble with no source processes this (sized at prepare)
    juce::AudioBuffer<float> tailScratch;      // a tail that must add rather than write renders here (sized at prepare)
    juce::AudioBuffer<float> sourceView;       // a Rumble's view of its source's block (no allocation)

    int   current = -1;          // the engine the last rendered block resolved (-1: none, the next block resets)
    float lastRumbleEnv = 1.0f;  // reused while the envelope's lock is contended
    bool  parked = true;         // not rendered live in the last rendered block (or prepared since)

    std::array<float, kMaxDests> lastResolved {};   // the last block's resolved values (C12: held on a matrix-lock miss)
    int   lastResolvedEngine = -1;                  // the engine they belong to
    bool  haveResolved = false;                     // a resolve of that engine has run with the lock held
};
JUCE_END_IGNORE_WARNINGS_MSVC

} // namespace mu_on
