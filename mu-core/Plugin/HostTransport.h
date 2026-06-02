#pragma once
// Shared helper for reading host DAW transport from juce::AudioPlayHead.
// All mu-family products call readHostTransport(getPlayHead()) at the top of
// their processBlock to derive play state and BPM before snapshotting blk* fields.
//
// Keeping the logic in one place means new products cannot accidentally omit it
// (the same omission caused mu-tant #837 — gate silence in all DAW hosts).

#include <juce_audio_processors/juce_audio_processors.h>

namespace mu_core {

// Transport state derived from the host for a single audio block.
struct HostTransport
{
    bool   playing = false;
    double bpm     = 0.0;   // 0 = host did not supply a BPM; caller uses fallback
};

// Reads the host DAW transport state from ph. Returns {false, 0.0} when ph is
// null (standalone / no playhead) or when the host does not supply position info.
// Call once per processBlock on the audio thread.
inline HostTransport readHostTransport(juce::AudioPlayHead* ph)
{
    HostTransport result;
    if (ph)
        if (auto pos = ph->getPosition())
        {
            result.playing = pos->getIsPlaying();
            if (auto hostBpm = pos->getBpm())
                result.bpm = *hostBpm;
        }
    return result;
}

} // namespace mu_core
