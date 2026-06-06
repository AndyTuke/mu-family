#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Link/MuLinkProtocol.h"

// MuLinkPlayHead — a juce::AudioPlayHead that reports the mu-link master transport.
//
// This is the whole trick behind L5 transport-slaving: PluginProcessor::deriveTransport()
// already reads play state + position from getPlayHead(). By handing the standalone
// processor this playhead while attached to mu-link, the sequencer follows the mu-link
// clock with NO change to processBlock — it's the same code path a DAW host drives.
//
// The snapshot is set on the render thread immediately before each processBlock call (same
// thread), so a plain member needs no synchronisation.
namespace mu_clid
{

class MuLinkPlayHead : public juce::AudioPlayHead
{
public:
    void setSnapshot(const mu_link::TransportSnapshot& s) noexcept { snap = s; }

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
    mu_link::TransportSnapshot snap;
};

} // namespace mu_clid
