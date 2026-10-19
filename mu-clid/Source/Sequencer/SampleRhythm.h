#pragma once

#include "Sequencer/GeneratorAliases.h"     // Rhythm, RhythmPlayState, EuclidOverrides
#include "Audio/VoiceEngine.h"               // mu-core: the sample voice chain
#include "Audio/MidiOutputEngine.h"          // mu-core: the MIDI output mode's note engine
#include "Audio/MixerEngine.h"               // mu-core: the mixer channel the modulation pass seeds and drives
#include "Modulation/ModulationDestinations.h"   // ModDest::kTableSize
#include "Modulation/ModulationSnapshot.h"   // kSnapCount
#include "Sequencer/RhythmCursor.h"          // the rhythm's own step cursor
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
// Being built up stage by stage: the processor creates every layer as a SampleRhythm and calls its
// per-block steps; until E5.7 the processor's per-slot arrays still hold the live engines (so `voice`,
// `midi` and the retired engines below are not used yet). Not copyable (atomics, owned engines). An assignment
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

    // Audio thread, once per block for a live rhythm, under the host's layer lock: the modulation pass. `strip`
    // is the rhythm's mixer channel (its pan and sends seed the matrix; the modulated values go back to it),
    // the modulated Euclid values recompute the pattern the hits come from (the rhythm's own cursor when
    // `cursorDrives`, with `clock`'s last step; else `sequencer`'s slot `slot`), and `engine` (null: none)
    // takes the block's modulated voice parameters.
    void applyModulation(double beatPos, MixerEngine::ChannelState& strip,
                         RhythmSequencer& sequencer, int slot, const RhythmStepClock& clock, bool cursorDrives,
                         VoiceEngine* engine);

    // Audio thread: the hit the sequencer fired for this rhythm this block. In MIDI output mode it becomes one
    // note-on in `midiOut` through `midiEngine` (never the voice); otherwise `engine` (null: none) retriggers, tied when
    // the sequencer marked the hit tied and the rhythm has pattern legato on. Allocates nothing itself.
    void fireHit(bool accented, bool tied, juce::MidiBuffer& midiOut, MidiOutputEngine& midiEngine, VoiceEngine* engine);

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

    std::unique_ptr<VoiceEngine> voice;   // the engine the rhythm's hits play (null until prepared)
    MidiOutputEngine             midi;    // used instead of the voice while the rhythm is in MIDI output mode

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

public:
    // The rhythm's own step cursor: built and handed patterns alongside the processor's RhythmSequencer;
    // its hits drive the voice only while the processor's useLayerRender switch is on (E5.5b).
    RhythmCursor cursor;

    // Engines a preset swap retired, still rendering their tails until drained (E5.4d moves these into
    // the shared RetireBin).
    std::array<std::unique_ptr<VoiceEngine>, kMaxRetiredEngines> retiredEngines;
    std::array<std::atomic<bool>, kMaxRetiredEngines>            retiredReadyForCleanup {};
};

} // namespace mu_clid
