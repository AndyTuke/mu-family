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
// `RhythmSequencer::MaxRhythms = mu_limits::kMaxLayers`) pointing here.
//
// Per-target capacity. mu-core is an INTERFACE library, so every target compiles its own copy of it and
// may set these macros for itself (a target definition, never an edit here): the four products and
// mu-core-tests keep the defaults, a 16-layer host defines MU_MAX_LAYERS=16 (mu-core-tests-16 does).
// One constant, one value per binary: there is no second "framework maximum".
#ifndef MU_MAX_LAYERS
 #define MU_MAX_LAYERS 8
#endif
#ifndef MU_MAX_BUSES
 #define MU_MAX_BUSES 0
#endif
#ifndef MU_BUS_INSERTS
 #define MU_BUS_INSERTS 2
#endif

namespace mu_limits
{
    // Maximum simultaneous channels (layers). The mixer strip count and
    // per-channel array sizing across the plugin track this — conceptually the
    // same number (one mixer strip per channel). A "channel" is a rhythm in
    // mu-clid, a voice in mu-tant.
    inline constexpr int kMaxLayers = MU_MAX_LAYERS;
    // The layer view's frozen mask and the bin's slot loops use 32-bit masks.
    static_assert(kMaxLayers >= 1 && kMaxLayers <= 32, "MU_MAX_LAYERS must be 1..32");

    // Group buses in the shared mixer and the inserts on each. Declared for the bus stage; nothing reads
    // them yet (0 buses everywhere).
    inline constexpr int kMaxBuses   = MU_MAX_BUSES;
    inline constexpr int kBusInserts = MU_BUS_INSERTS;

    // Layers shown at once in the sidebar and the mixer (a 16-layer host pages them in two).
    inline constexpr int kLayersPerPage = 8;

    // Stored clips per layer (the clip bank a controller pad launches from).
    inline constexpr int kClipsPerLayer = 8;

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

    // ─── The layer render contract (docs/mu-tronic/design.md 2.1) ────────────────────────────────
    // Entries per slot in the shared RetireBin: mu-Clid's retired-engine depth, so two retargets inside
    // one fade never drop, free early or block.
    inline constexpr int kRetireDepth = kMaxRetiredVoiceEngines;
    // Tails sounding at once in one instance. The default never binds (it is the bin's capacity); a host
    // with many layers lowers it from its CPU measurements, and the oldest tail is then evicted.
    inline constexpr int kMaxLiveRetirees = kMaxLayers * kRetireDepth;
    // Hard lifetime of any tail (reverb and comb tails that never report silence), and the declick
    // applied in its last milliseconds.
    inline constexpr double kRetireHardCapSeconds = 30.0;
    inline constexpr double kRetireDeclickMs = 10.0;
    // MIDI events one host block may carry into the layers (decoded once per block). The last
    // kMidiNoteOffReserve entries are kept for note-offs a layer or a tail owes, so an overflow of the
    // input can never strand a held note.
    inline constexpr int kMaxBlockMidiEvents = 2048;
    inline constexpr int kMidiNoteOffReserve = 256;
    static_assert(kMidiNoteOffReserve < kMaxBlockMidiEvents, "the note-off reserve is part of the block's storage");
    // Channel messages kept from host blocks that rendered nothing, for a processor that carries them into its
    // next rendered block (ProcessorBase::setCarrySkippedMidi). They sit at the front of the block's input.
    inline constexpr int kMaxCarriedMidiEvents = 512;
    static_assert(kMaxCarriedMidiEvents < kMaxBlockMidiEvents - kMidiNoteOffReserve, "the carried events are part of the input");
    // Published layer views (Published access mode): the live view, at most two in their grace period,
    // and one free for the next publication.
    inline constexpr int kLayerViewPool = 4;

    // MIDI program-change FIFO depth. Audio thread enqueues incoming PCs;
    // runDeferredWork drains on the message thread.
    inline constexpr int kProgramChangeFifoSize = 32;

    // First N channels expose full "<Layer> N " APVTS parameter names so DAW
    // automation lanes are readable; remaining channels use short "N " names.
    // Raise (up to kMaxLayers) to expose more channels with full names.
    inline constexpr int kMaxAutomatedChannels = 3;

    // Items in one Performance file (a list of full-preset pointers a controller's top row switches between).
    inline constexpr int kMaxPerformanceItems = 8;
}
