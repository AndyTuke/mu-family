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
//   3. Standalone with MIDI clock sync on: the clock owns play / stop, tempo and beat.
//   4. Otherwise the product's own transport (its Play button, BPM field and beat counter).
//
// Whenever the source is outside (1–3) the Play button mirrors it, and the internal beat counter
// carries the beat on, so the UI position is live and the own transport resumes seamlessly if
// the source goes away. A product may bound its beat space (`wrapBeats`, e.g. mu-Tant's longest
// pattern) — the block's start beat and the stored counter are then wrapped into it.
namespace mu_core
{

struct BlockTransport
{
    enum class Source { Host, HostTempo, MidiClock, Internal };

    Source source         = Source::Internal;
    bool   playing        = false;
    double bpm            = 120.0;
    double startBeat      = 0.0;   // the beat at this block's first sample
    double beatsPerSample = 0.0;
    double blockBeats     = 0.0;   // beats this block spans (0 while stopped)

    bool   beatFromOutside() const noexcept { return source == Source::Host || source == Source::MidiClock; }
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
                                       int numSamples, double sampleRate, double wrapBeats = 0.0)
{
    BlockTransport t;
    const double ownBpm = internal.bpm.load(std::memory_order_relaxed);

    // Pick the source (the family rule above).
    if (host.hasPosition)
    {
        t.source    = BlockTransport::Source::Host;
        t.playing   = host.playing;
        t.bpm       = host.bpm > 0.0 ? host.bpm : ownBpm;
        t.startBeat = host.ppqPosition;
    }
    else if (! isStandalone)
    {
        t.source    = BlockTransport::Source::HostTempo;
        t.playing   = host.playing;
        t.bpm       = host.bpm > 0.0 ? host.bpm : ownBpm;
        t.startBeat = internal.beatPos.load(std::memory_order_relaxed);
    }
    else if (clock.isEnabled())
    {
        t.source    = BlockTransport::Source::MidiClock;
        t.playing   = clock.isPlaying();
        t.bpm       = clock.getBpm() > 0.0 ? clock.getBpm() : ownBpm;
        t.startBeat = clockBlockBeat;
    }
    else
    {
        t.source    = BlockTransport::Source::Internal;
        t.playing   = internal.playing.load(std::memory_order_relaxed);
        t.bpm       = ownBpm;
        t.startBeat = internal.beatPos.load(std::memory_order_relaxed);
    }

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

    // Mirror an outside transport into the Play button, and carry the beat on past this block. A
    // stopped own transport leaves the counter alone (the UI may be resetting it).
    if (t.source != BlockTransport::Source::Internal)
        internal.playing.store(t.playing, std::memory_order_relaxed);
    if (t.playing || t.beatFromOutside())
        internal.beatPos.store(wrap(t.startBeat + t.blockBeats), std::memory_order_relaxed);
    return t;
}

} // namespace mu_core
