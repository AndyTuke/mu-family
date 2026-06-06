#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Link/MuLinkProtocol.h"

// MuLinkPlayHead — a juce::AudioPlayHead that reports the mu-link master transport. Shared
// by every product's standalone (lives in mu-core/Link).
//
// This is the whole trick behind transport-slaving: a product's processBlock reads play
// state + position from getPlayHead() (the family standard — see mu-core/Plugin/
// HostTransport.h). Hand the standalone processor this playhead while attached to mu-link
// and the sequencer follows the mu-link clock with NO processBlock change — the same path a
// DAW host drives.
//
// The snapshot is set on the render thread immediately before each processBlock call (same
// thread), so a plain member needs no synchronisation.
namespace mu_link
{

class MuLinkPlayHead : public juce::AudioPlayHead
{
public:
    void setSnapshot(const TransportSnapshot& s) noexcept { snap = s; }

    juce::Optional<PositionInfo> getPosition() const override
    {
        const double sr      = snap.sampleRate != 0 ? (double) snap.sampleRate : 48000.0;
        const double seconds = (double) snap.samplePos / sr;
        const double beats   = seconds * (snap.tempoBpm / 60.0);   // ppq = quarter notes = beats

        PositionInfo info;
        info.setIsPlaying(snap.playing != 0);
        info.setTimeInSamples((juce::int64) snap.samplePos);
        info.setTimeInSeconds(seconds);
        info.setPpqPosition(beats);
        info.setBpm(snap.tempoBpm);
        return info;
    }

private:
    TransportSnapshot snap;
};

} // namespace mu_link
