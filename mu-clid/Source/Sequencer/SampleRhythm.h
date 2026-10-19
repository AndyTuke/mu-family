#pragma once

#include "Sequencer/GeneratorAliases.h"     // Rhythm, RhythmPlayState, EuclidOverrides
#include "Audio/VoiceEngine.h"               // mu-core: the sample voice chain
#include "Audio/MidiOutputEngine.h"          // mu-core: the MIDI output mode's note engine
#include "Modulation/ModulationSnapshot.h"   // kSnapCount
#include "MuLimits.h"

#include <array>
#include <atomic>
#include <memory>

// mu-Clid's layer type (docs/mu-tronic/extraction-plan.md 2.5, E5.3): the shared Euclid Rhythm plus what
// a sample-playing rhythm keeps per layer: its voice engine, its MIDI output engine, the play state the
// UI draws, the live modulation snapshot, its sample path, the modulation pass's previous-block memory and
// the retired engines still sounding their tails. mu_core::Rhythm itself, and mu-Clid MIDI, are untouched.
//
// Being built up stage by stage: the processor creates every layer as a SampleRhythm, but these members
// are not used yet; the processor's per-slot arrays still hold the live engines and state. E5.4 moves the
// per-block work in, and E5.7 deletes the arrays. Not copyable (atomics, owned engines). An assignment
// through Rhythm& (a preset install, a reset to defaults) changes only the Rhythm part, so what is below
// stays with the slot, as the processor's arrays do.
namespace mu_clid
{

struct SampleRhythm : Rhythm
{
    static constexpr int kMaxRetiredEngines = mu_limits::kMaxRetiredVoiceEngines;

    SampleRhythm() = default;
    SampleRhythm(const SampleRhythm&)            = delete;
    SampleRhythm& operator=(const SampleRhythm&) = delete;

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

    // Engines a preset swap retired, still rendering their tails until drained (E5.4d moves these into
    // the shared RetireBin).
    std::array<std::unique_ptr<VoiceEngine>, kMaxRetiredEngines> retiredEngines;
    std::array<std::atomic<bool>, kMaxRetiredEngines>            retiredReadyForCleanup {};
};

} // namespace mu_clid
