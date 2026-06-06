#pragma once
// Shared helper for reading host DAW transport from juce::AudioPlayHead.
// All mu-family products call readHostTransport(getPlayHead()) at the top of
// their processBlock to derive play state and BPM before snapshotting blk* fields.
//
// Keeping the logic in one place means new products cannot accidentally omit it —
// omitting host-transport reads causes gate silence in all DAW plugin modes.

#include <juce_audio_processors/juce_audio_processors.h>

namespace mu_core {

// Transport state derived from the host (or, in standalone, an injected playhead) for a
// single audio block. `hasPosition`/`ppqPosition` carry the beat position, which a product
// uses to *slave* its sequencer — to a DAW host, or to mu-link via MuLinkPlayHead when the
// standalone is attached to the bus.
struct HostTransport
{
    bool   playing     = false;
    double bpm         = 0.0;   // 0 = no BPM supplied; caller uses fallback
    bool   hasPosition = false; // true when the playhead supplied a beat position
    double ppqPosition = 0.0;   // beats (quarter notes) since the timeline origin
};

// Reads the playhead transport from ph. Returns a default (no position) when ph is null
// (standalone with no host/bus) or the playhead supplies nothing. Call once per processBlock
// on the audio thread.
//
// **Family standard:** every product calls this at the top of processBlock — in BOTH plugin
// and standalone modes — and slaves to `ppqPosition` when `hasPosition`, falling back to its
// own internal transport otherwise. In standalone this is normally null (internal transport),
// but becomes the mu-link master when MuLinkBridge injects a MuLinkPlayHead.
inline HostTransport readHostTransport(juce::AudioPlayHead* ph)
{
    HostTransport result;
    if (ph)
        if (auto pos = ph->getPosition())
        {
            result.playing = pos->getIsPlaying();
            if (auto hostBpm = pos->getBpm())
                result.bpm = *hostBpm;
            if (auto ppq = pos->getPpqPosition())
            {
                result.hasPosition = true;
                result.ppqPosition = *ppq;
            }
        }
    return result;
}

} // namespace mu_core
