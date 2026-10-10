#pragma once

#include "Plugin/HostTransport.h"
#include "Plugin/MidiClockSync.h"
#include <atomic>
#include <cmath>

// The family transport rule — one place decides where a block's play state, tempo and beat
// come from, for every product:
//
//   1. A playhead with a POSITION (a DAW host, or mu-link through its injected playhead):
//      play, tempo and beat all follow it.
//   2. Inside a host that gives no position: play + tempo follow the host; the beat runs on.
//   3. Standalone with MIDI clock sync on: each half follows its Messages setting — play / stop
//      from the clock's Start / Continue / Stop when transport messages are on (else the Play
//      button), tempo and beat from the clock ticks when ticks are on (else the BPM field, with
//      the own beat restarted at each Start).
//   4. Otherwise the product's own transport (its Play button, BPM field and beat counter).
//
// Whenever play comes from outside the Play button mirrors it, and whenever the beat does the
// internal beat counter carries it on, so the UI position is live and the own transport resumes seamlessly if
// the source goes away. A product may bound its beat space (`wrapBeats`, e.g. mu-Tant's longest
// pattern) — the block's start beat and the stored counter are then wrapped into it.
namespace mu_core
{

struct BlockTransport
{
    enum class Source { Host, HostTempo, MidiClock, Internal };

    Source source         = Source::Internal;
    bool   playOutside    = false;   // play state came from the host / clock, not the Play button
    bool   beatOutside    = false;   // beat came from the host / clock, not the own counter
    bool   playing        = false;
    double bpm            = 120.0;
    double startBeat      = 0.0;   // the beat at this block's first sample
    double beatsPerSample = 0.0;
    double blockBeats     = 0.0;   // beats this block spans (0 while stopped)

    bool   beatFromOutside() const noexcept { return beatOutside; }
};

// The product's own transport state (its atomics, shared with the UI).
struct InternalTransport
{
    std::atomic<bool>&   playing;
    std::atomic<double>& bpm;
    std::atomic<double>& beatPos;
};

// Resolve this block's transport and advance the internal beat counter past it. Audio thread,
// once per processBlock. `clockBlockBeat` is MidiClockSync::process's return for this block.
inline BlockTransport resolveTransport(const HostTransport& host, bool isStandalone,
                                       const MidiClockSync& clock, double clockBlockBeat,
                                       InternalTransport internal,
                                       int numSamples, double sampleRate, double wrapBeats = 0.0,
                                       double syncOffsetMs = 0.0)
{
    BlockTransport t;
    bool relocated = false;   // the clock moved the own beat this block (store it even while stopped)
    const double ownBpm = internal.bpm.load(std::memory_order_relaxed);

    // Pick the source (the family rule above).
    if (host.hasPosition)
    {
        t.source    = BlockTransport::Source::Host;
        t.playOutside = t.beatOutside = true;
        t.playing   = host.playing;
        t.bpm       = host.bpm > 0.0 ? host.bpm : ownBpm;
        t.startBeat = host.ppqPosition;
    }
    else if (! isStandalone)
    {
        t.source    = BlockTransport::Source::HostTempo;
        t.playOutside = true;
        t.playing   = host.playing;
        t.bpm       = host.bpm > 0.0 ? host.bpm : ownBpm;
        t.startBeat = internal.beatPos.load(std::memory_order_relaxed);
    }
    else if (clock.isEnabled())
    {
        t.source      = BlockTransport::Source::MidiClock;
        t.playOutside = clock.transportDrives();
        t.beatOutside = clock.ticksDrive();
        t.playing     = t.playOutside ? clock.isPlaying() : internal.playing.load(std::memory_order_relaxed);
        // A lost clock holds the transport stopped (owner rule) until pulses return. In Clock only
        // the Play button keeps its state; with transport messages on it mirrors the held stop.
        // Either way play resumes with the clock.
        if (clock.isLost())
            t.playing = false;
        if (t.beatOutside)
        {
            t.bpm       = clock.getBpm() > 0.0 ? clock.getBpm() : ownBpm;
            t.startBeat = clockBlockBeat;
        }
        else
        {
            // Transport only: the own tempo and beat, moved by the clock's Start (bar 1) or Song
            // Position Pointer.
            t.bpm       = ownBpm;
            relocated   = clock.startedInBlock() || clock.locatedInBlock();
            t.startBeat = relocated ? clockBlockBeat : internal.beatPos.load(std::memory_order_relaxed);
        }
    }
    else
    {
        t.source    = BlockTransport::Source::Internal;
        t.playing   = internal.playing.load(std::memory_order_relaxed);
        t.bpm       = ownBpm;
        t.startBeat = internal.beatPos.load(std::memory_order_relaxed);
    }

    // Sync offset: under an external MIDI clock the app plays `syncOffsetMs` earlier than the clock says,
    // to cancel the audio output latency (positive = earlier). The block's span is unchanged.
    if (t.source == BlockTransport::Source::MidiClock && t.beatOutside)
        t.startBeat = std::max(0.0, t.startBeat + syncOffsetMs * t.bpm / 60000.0);

    // Bound the beat space (a host position can be any size, or negative in a pre-roll).
    auto wrap = [wrapBeats](double b)
    {
        if (wrapBeats <= 0.0) return b;
        b = std::fmod(b, wrapBeats);
        return b < 0.0 ? b + wrapBeats : b;
    };
    t.startBeat      = wrap(t.startBeat);
    t.beatsPerSample = sampleRate > 0.0 ? (t.bpm / 60.0) / sampleRate : 0.0;
    t.blockBeats     = t.playing ? t.beatsPerSample * (double) numSamples : 0.0;

    // Under MIDI clock the block spans exactly what the clock's beat model moved: its end beat
    // is where the next block starts, so consecutive blocks are contiguous (a phase correction
    // can't make one overlap the last), and the in-block rate is that span per sample.
    if (t.source == BlockTransport::Source::MidiClock && t.beatOutside && t.playing && numSamples > 0)
    {
        // A jump (the clock snapped after a long gap or a loop) is capped at twice the nominal span,
        // so the sequencers don't fire a whole backlog in one block.
        const double nominalSpan = t.beatsPerSample * (double) numSamples;
        t.blockBeats     = std::clamp(clock.getBlockEndBeat() - clockBlockBeat, 0.0, 2.0 * nominalSpan);
        t.beatsPerSample = t.blockBeats / (double) numSamples;
    }

    // Mirror an outside transport into the Play button, and carry the beat on past this block. A
    // stopped own transport leaves the counter alone (the UI may be resetting it).
    if (t.playOutside)
        internal.playing.store(t.playing, std::memory_order_relaxed);
    if (t.playing || t.beatFromOutside() || relocated)
        internal.beatPos.store(wrap(t.startBeat + t.blockBeats), std::memory_order_relaxed);
    return t;
}

} // namespace mu_core
