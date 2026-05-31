#pragma once

// Project-wide capacity invariants. Anything that caps how many of a thing
// can exist simultaneously across the whole plugin lives here — bumping a
// limit is a one-line edit.
//
// Scope: ONLY capacity invariants belong here. Per-module tuning constants
// (gain trims, filter band centres, envelope time constants, etc.) stay
// next to the code they tune.
//
// Naming is plugin-agnostic ("channel" / "layer" / "slot"), since mu-core is
// shared. Each product may re-export a domain alias (e.g. mu-clid's
// `SequencerEngine::MaxRhythms = mu_limits::kMaxChannels`) pointing here.
namespace mu_limits
{
    // Maximum simultaneous channels (layers). The mixer strip count and
    // per-channel array sizing across the plugin track this — conceptually the
    // same number (one mixer strip per channel). A "channel" is a rhythm in
    // mu-clid, a voice in mu-tant.
    inline constexpr int kMaxChannels = 8;

    // Per-channel modulator (ControlSequence) count. The UI tab count in
    // ModulatorPanel mirrors this; the two must stay equal.
    inline constexpr int kMaxControlSequences = 8;

    // Maximum modulation assignments per ModulationMatrix (per channel).
    inline constexpr int kMaxModulationAssignments = 64;

    // Per-channel polyphony for the SamplePlayer voice pool.
    inline constexpr int kMaxSamplerVoices = 4;

    // Stage 34: per-channel retired-voice-engine slots for the polyphonic
    // hot-swap tail. Each retired engine keeps rendering its in-flight
    // sample / envelope until it reports isFullyDrained.
    inline constexpr int kMaxRetiredVoiceEngines = 4;

    // MIDI program-change FIFO depth. Audio thread enqueues incoming PCs;
    // handleAsyncUpdate drains on the message thread.
    inline constexpr int kProgramChangeFifoSize = 32;

    // First N channels expose full "<Layer> N " APVTS parameter names so DAW
    // automation lanes are readable; remaining channels use short "N " names.
    // Raise (up to kMaxChannels) to expose more channels with full names.
    inline constexpr int kMaxAutomatedChannels = 3;
}
