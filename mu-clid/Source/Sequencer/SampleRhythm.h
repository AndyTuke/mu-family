#pragma once

#include "Sequencer/GeneratorAliases.h"     // Rhythm, RhythmPlayState, EuclidOverrides
#include "Audio/VoiceEngine.h"               // mu-core: the sample voice chain
#include "Audio/MidiOutputEngine.h"          // mu-core: the MIDI output mode's note engine
#include "Audio/MixerEngine.h"               // mu-core: the mixer channel the modulation pass seeds and drives
#include "Modulation/ModulationDestinations.h"   // ModDest::kTableSize
#include "Modulation/ModulationSnapshot.h"   // kSnapCount
#include "Sequencer/RhythmCursor.h"          // the rhythm's own step cursor
#include "Sequencer/LayerTypeInfo.h"         // mu-core: LayerTypeId
#include "Sequencer/RenderContext.h"         // mu-core: the render contract's per-call context
#include "MuLimits.h"

#include <array>
#include <atomic>
#include <memory>
#include <string_view>
#include <unordered_map>

// mu-Clid's layer type (docs/mu-tronic/extraction-plan.md 2.5, E5.3): the shared Euclid Rhythm plus what
// a sample-playing rhythm keeps per layer: its voice engine, its MIDI output engine, the play state the
// UI draws, the live modulation snapshot, its sample path, the modulation pass's previous-block memory and
// the retired engines still sounding their tails. mu_core::Rhythm itself, and mu-Clid MIDI, are untouched.
//
// Being built up stage by stage: the processor creates every layer as a SampleRhythm; the layer owns its voice,
// MIDI and retired engines (E5.5c, E5.5d) and renders through the render contract, while the processor still
// decides each block's hit and keeps the play state the UI reads (to E5.7). Not copyable (atomics, owned engines). An assignment
// through Rhythm& (a preset install, a reset to defaults) changes only the Rhythm part, so what is below
// stays with the slot; a reorder moves the whole layer, members included (the host then resets the moved
// slots' modulation memory, resetModulationMemory).
namespace mu_clid
{

struct SampleRhythm : Rhythm
{
    static constexpr int kMaxRetiredEngines = mu_limits::kMaxRetiredVoiceEngines;

    SampleRhythm();
    SampleRhythm(const SampleRhythm&)            = delete;
    SampleRhythm& operator=(const SampleRhythm&) = delete;

    // mu_core::Layer (E5.5d): a Rhythm layer. prepare readies the engines the layer holds (its voice, its MIDI
    // engine and the engines its swaps retired); render plays the block on the voice.
    int  typeId() const override { return (int) mu_core::LayerTypeId::Rhythm; }
    void prepare(double sampleRate, int blockSize) override;
    // Audio thread, under the host's layer lock. The block's first call (frameOffset 0) retriggers the voice
    // when `block` holds a hit, then runs the modulation pass; every call then renders the voice and the
    // retired engines into `out`.
    void render(const mu_core::RenderContext& ctx, juce::AudioBuffer<float>& out, int n) override;

    // What the host hands the rhythm for one rendered block, before any layer renders (audio thread, the
    // host's layer lock held). Plain values; render consumes them in the block's first call.
    struct BlockInputs
    {
        bool voiceHit = false;                       // the cursor found a hit this block and it plays on the voice
        bool accented = false;
        bool tied     = false;
        int  lastEffectiveStep = -1;                 // the host clock's step (a modulated pattern length re-derives from it)
        MixerEngine::ChannelState* strip = nullptr;  // the rhythm's mixer channel this block (null: no modulation pass)
    };
    BlockInputs block;

    // Audio thread, once per block for a live rhythm, under the host's layer lock (render's first call): the
    // modulation pass. `strip` is the rhythm's mixer channel (its pan and sends seed the matrix; the modulated
    // values go back to it), the modulated Euclid values recompute the cursor's pattern (with the host clock's
    // `lastEffectiveStep`), and `engine` (null: none) takes the block's modulated voice parameters.
    void applyModulation(double beatPos, MixerEngine::ChannelState& strip, int lastEffectiveStep, VoiceEngine* engine);

    // Audio thread, from the host's block start: the hit the rhythm's cursor found this block. In MIDI output
    // mode it becomes one note-on in `midiOut` through the rhythm's own `midi` engine (never the voice), now;
    // otherwise it waits in `block` for render, which retriggers the voice (tied when the hit is tied and the
    // rhythm has pattern legato on) before the modulation pass. Allocates nothing itself.
    void takeHit(bool accented, bool tied, juce::MidiBuffer& midiOut);

    // Audio thread, once per playing block: the play state the UI draws (step and fraction as one snapshot, the
    // pattern and ring lengths, and the monotonic hit counter when `fired`).
    void publishPlayState(RhythmPlayState& state, int step, int accentStep, int patternLength, float frac,
                          bool fired) const noexcept;

    // The Euclid values after modulation, for the UI: the base values while nothing is assigned (the
    // overrides are only written when a pass runs). Integer-rounded by the audio thread, so a torn read is
    // harmless for display.
    EuclidOverrides modulatedEuclidOverrides() const noexcept;

    // Message thread, under the host's layer lock: the slot's tenant changed, so the next block re-runs a
    // modulated rhythm's pass and pattern recompute instead of trusting the previous tenant's memory.
    void resetModulationMemory() noexcept;

    // The engine the rhythm's hits play (E5.5d: owned by the layer, so it moves with it). Null in a slot that is
    // not live; built by the host outside every lock and handed over under it.
    std::unique_ptr<VoiceEngine> voice;
    // Sends the note-on and, 20 ms later, the note-off while the rhythm is in MIDI output mode. Live state
    // (E5.5c): it moves with the layer; a removed layer's owed note-off is handed to the host (layerRemoved).
    MidiOutputEngine             midi;

    // Written by the audio thread, read by the UI.
    RhythmPlayState                          playState;
    std::array<std::atomic<float>, kSnapCount> modSnapshot {};   // ModulationSnapshot.h indices

    juce::String samplePath;   // message thread only; empty = no sample

    // The modulation pass's memory of the previous block (audio thread): the overrides it applied, the
    // ones before them, and whether the matrix had assignments (a transition to empty runs one last pass).
    EuclidOverrides lastEuclidOverrides {};
    EuclidOverrides prevEuclidOverrides {};
    bool            prevMatrixHadAssignments = false;

private:
    // The modulation pass's phases (audio thread; seed, strip and snapshot run under modLock where noted).
    struct StripMod { float pan, effect, delay, reverb; };   // modulated mixer-strip values
    void     seedModulation(const VoiceParams& modParams, const MixerEngine::ChannelState& strip);
    StripMod applyStripModulation(MixerEngine::ChannelState& strip);
    void     publishModSnapshot(const VoiceParams& modParams, const StripMod& stripMod);
    void     writeBackModulation(VoiceParams& modParams);

    // The matrix's working values, one per destination id, built in the constructor so the audio thread
    // never inserts (keys are string literals, so the views stay valid); modSlot reaches each by its
    // ModDest::kTable index without hashing.
    std::unordered_map<std::string_view, float> modParamValues;
    std::array<float*, ModDest::kTableSize>     modSlot {};
    float& mv(int destIndex) noexcept { jassert(modSlot[(size_t) destIndex] != nullptr); return *modSlot[(size_t) destIndex]; }

    // Audio thread: the voice, then each retired engine (raising its drained flag), added into `out`.
    void renderVoices(juce::AudioBuffer<float>& out, int n);

public:
    // The rhythm's own step cursor: it decides the rhythm's hits from the host's step clock.
    RhythmCursor cursor;

    // Engines a preset swap retired, still rendering their tails after the voice until drained; the audio thread
    // raises the flag once one reports drained, and the host's deferred-work pass frees it (E5.5e moves these
    // into the shared RetireBin).
    std::array<std::unique_ptr<VoiceEngine>, kMaxRetiredEngines> retiredEngines;
    std::array<std::atomic<bool>, kMaxRetiredEngines>            retiredReadyForCleanup {};
};

} // namespace mu_clid
