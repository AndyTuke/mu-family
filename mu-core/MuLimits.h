#pragma once

// Project-wide capacity invariants. Anything that caps how many of a thing
// can exist simultaneously across the whole plugin lives here — bumping a
// limit is a one-line edit.
//
// Scope: ONLY capacity invariants belong here. Per-module tuning constants
// (gain trims, filter band centres, envelope time constants, etc.) stay
// next to the code they tune.
//
// Naming is plugin-agnostic ("channel" / "layer" / "slot"), since mu-core is shared.
//
// There is no per-product capacity here. mu-core is compiled once, and each product decides its counts when its processor is
// constructed (ProcessorLimits below, passed to ProcessorBase and to its parameter layout), because the parameters are declared
// then. This header holds only the fixed ceilings those counts must stay within, and the constants every product shares.

namespace mu_limits
{
    // Fixed ceilings. A count a product chooses (below) can never exceed these: the layer masks are 32-bit and the
    // parameter ids carry one digit for a bus and one for a bus insert. They bound the few small fixed arrays that
    // stay fixed; every loop runs to the runtime count, never to a ceiling.
    inline constexpr int kAbsoluteMaxLayers = 32;
    inline constexpr int kAbsoluteMaxBuses  = 9;
    inline constexpr int kAbsoluteBusInserts = 9;

    // The counts one processor is built with. Each product owns one constant and passes it to its parameter layout and to
    // ProcessorBase, so the two cannot disagree; ProcessorBase keeps it (getLimits()) and everything that needs a count
    // reads it from there. 0 buses declares no bus parameters and shows no mixer Buses page. externalSidechain is false for a
    // Standalone-only product (no host sidechain bus), so the mixer does not list a source that can never carry audio.
    struct ProcessorLimits
    {
        int  layers = 8;
        int  buses = 0;
        int  busInserts = 2;
        bool externalSidechain = true;

        constexpr bool valid() const noexcept
        {
            return layers >= 1 && layers <= kAbsoluteMaxLayers
                && buses >= 0 && buses <= kAbsoluteMaxBuses
                && busInserts >= 1 && busInserts <= kAbsoluteBusInserts;
        }
    };

    // Layers shown at once in the sidebar and the mixer (a 16-layer host pages them in two).
    inline constexpr int kLayersPerPage = 8;

    // Stored clips per layer (the clip bank a controller pad launches from).
    inline constexpr int kClipsPerLayer = 8;

    // Per-channel modulator (ControlSequence) count. The UI tab count in
    // ModulatorPanel mirrors this; the two must stay equal.
    inline constexpr int kMaxControlSequences = 8;

    // The largest decoded loop a layer holds, in bytes (float32 x channels x frames): 30 s of stereo at 48 kHz, 15 s at
    // 96 kHz, 60 s mono. A loop over it is rejected before decoding, never truncated (docs/mu-tilate/design.md).
    inline constexpr long long kMaxLoopBytes = 11520000;

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
    inline constexpr int kMaxLiveRetirees = kAbsoluteMaxLayers * kRetireDepth;
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
    // Raise (up to the layer count) to expose more channels with full names.
    inline constexpr int kMaxAutomatedChannels = 3;

    // Items in one Performance file (a list of full-preset pointers a controller's top row switches between).
    inline constexpr int kMaxPerformanceItems = 8;
}
