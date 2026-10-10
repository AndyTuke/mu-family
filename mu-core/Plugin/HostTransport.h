#pragma once
// Shared helper for reading host DAW transport from juce::AudioPlayHead.
// All mu-family products call readHostTransport(getPlayHead()) at the top of
// their processBlock to derive play state and BPM before snapshotting blk* fields.
//
// Keeping the logic in one place means new products cannot accidentally omit it —
// omitting host-transport reads causes gate silence in all DAW plugin modes.

#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>

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

    // The host's meter and where the current bar began (4/4 from 0 when the host gives none).
    int    timeSigNumerator   = 4;
    int    timeSigDenominator = 4;
    bool   hasBarStart        = false;
    double barStartPpq        = 0.0;   // ppq of the current bar's downbeat
};

// A bar / beat / sixteenth position for display: bar and beat count from 1, a beat is one
// denominator note, and sub is the sixteenth within that beat (from 1).
struct BarPosition { int bar = 1, beat = 1, sub = 1; };

// Where ppq sits in a numerator/denominator meter. With a host bar start the bar grid is
// anchored on it (a pickup bar or a meter change earlier in the song still lands the downbeat
// right); without one bars run from beat 0. Assumes the meter is constant back to the start for
// the bar number.
inline BarPosition barPositionOf(double ppq, int numerator, int denominator,
                                 bool hasBarStart = false, double barStartPpq = 0.0)
{
    const int    num     = juce::jlimit(1, 64, numerator);
    const int    den     = juce::jlimit(1, 64, denominator);
    const double beatLen = 4.0 / den;                 // quarter notes per beat
    const double barLen  = beatLen * num;
    const double eps     = 1.0e-9;                    // a rounding hair below a boundary counts as on it

    // Anchor the bar grid on the host's downbeat when known, else on beat 0.
    const double anchor  = hasBarStart ? barStartPpq - std::floor(barStartPpq / barLen + eps) * barLen : 0.0;
    const double fromAnchor = ppq - anchor;
    const double barIndex   = std::floor(fromAnchor / barLen + eps);
    const double inBar      = juce::jmax(0.0, fromAnchor - barIndex * barLen);

    const int subsPerBeat = juce::jmax(1, 16 / den);
    BarPosition p;
    p.bar  = (int) barIndex + 1;
    p.beat = juce::jlimit(1, num, (int) std::floor(inBar / beatLen + eps) + 1);
    const double inBeat = juce::jmax(0.0, inBar - (p.beat - 1) * beatLen);
    p.sub  = juce::jlimit(1, subsPerBeat, (int) std::floor(inBeat / (beatLen / subsPerBeat) + eps) + 1);
    return p;
}

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
            if (auto sig = pos->getTimeSignature(); sig && sig->numerator > 0 && sig->denominator > 0)
            {
                result.timeSigNumerator   = sig->numerator;
                result.timeSigDenominator = sig->denominator;
            }
            if (auto bar = pos->getPpqPositionOfLastBarStart())
            {
                result.hasBarStart = true;
                result.barStartPpq = *bar;
            }
        }
    return result;
}

} // namespace mu_core
