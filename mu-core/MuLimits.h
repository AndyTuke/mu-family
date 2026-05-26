#pragma once

// Project-wide capacity invariants. Anything that caps how many of a thing
// can exist simultaneously across the whole plugin lives here — bumping a
// limit is a one-line edit.
//
// Scope: ONLY capacity invariants belong here. Per-module tuning constants
// (gain trims, filter band centres, envelope time constants, etc.) stay
// next to the code they tune.
//
// Each owning class re-exports its own historical name via a `static
// constexpr` alias so existing references like `SequencerEngine::MaxRhythms`
// keep compiling. The values live here.
namespace mu_limits
{
    // Maximum simultaneous rhythm slots. Mixer channel count and per-rhythm
    // array sizing across the plugin track this — they are conceptually the
    // same number (one mixer strip per rhythm).
    inline constexpr int kMaxRhythms = 8;

    // Per-rhythm modulator (ControlSequence) count. The UI tab count in
    // ModulatorPanel mirrors this; the two must stay equal.
    inline constexpr int kMaxControlSequences = 8;

    // Maximum modulation assignments per ModulationMatrix (per rhythm).
    inline constexpr int kMaxModulationAssignments = 64;

    // Per-rhythm polyphony for the SamplePlayer voice pool.
    inline constexpr int kMaxSamplerVoices = 4;

    // Stage 34: per-rhythm retired-voice-engine slots for the polyphonic
    // hot-swap tail. Each retired engine keeps rendering its in-flight
    // sample / envelope until it reports isFullyDrained.
    inline constexpr int kMaxRetiredVoiceEngines = 4;

    // MIDI program-change FIFO depth. Audio thread enqueues incoming PCs;
    // handleAsyncUpdate drains on the message thread.
    inline constexpr int kProgramChangeFifoSize = 32;

    // First N rhythms expose full "Rhythm N " APVTS parameter names so DAW
    // automation lanes are readable; remaining rhythms use short "RN " names.
    // Raise (up to kMaxRhythms) to expose more rhythms with full names.
    inline constexpr int kMaxAutomatedRhythms = 3;
}
