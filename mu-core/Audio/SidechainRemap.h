#pragma once

// Sidechain sources name a mixer channel (a layer's strip). When the layers are renumbered — one is
// removed and the ones above shift down, or two are swapped — every source that pointed at a moved
// layer has to be re-pointed, or a strip ducks from the wrong layer (or from itself). These are the
// pure renumbering rules; the products apply them to their own storage (the mixer engine's atomics
// or the APVTS parameters).
//
// A source is a plain int: -1 = off, 0..maxChannels-1 = a channel, maxChannels = the external DAW bus,
// maxChannels + 1 + b = group bus b. Off, the external bus and the buses never move: a layer edit
// renumbers channels only, so the bus range needs nothing more than the range check below.
namespace mu_mix
{

// Which kind of strip is asking for a sidechain source.
enum class StripKind { Channel, Bus, Return };

// The source value naming group bus `bus`.
inline constexpr int busSource(int bus, int maxChannels) noexcept { return maxChannels + 1 + bus; }

// True when `source` names a group bus (0..maxBuses-1 written to `busOut` if given).
inline bool isBusSource(int source, int maxChannels, int maxBuses, int* busOut = nullptr) noexcept
{
    const int b = source - maxChannels - 1;
    if (b < 0 || b >= maxBuses) return false;
    if (busOut != nullptr) *busOut = b;
    return true;
}

// Whether a strip may take `source`. The mixer graph is channels -> buses -> returns -> master, so a
// source must be built before the strip that listens to it:
//   channel strip: another channel, or the external input (never a bus: a bus is built after the channels)
//   bus strip:     a channel, a LOWER-index bus, or the external input
//   return strip:  a channel, any bus, or the external input
// `stripIndex` is the channel or bus index (ignored for returns). Off (-1) is always legal.
inline bool isLegalSidechainSource(StripKind kind, int stripIndex, int source, int maxChannels, int maxBuses) noexcept
{
    if (source < 0 || source == maxChannels) return true;
    if (source < maxChannels) return ! (kind == StripKind::Channel && source == stripIndex);
    int b = 0;
    if (! isBusSource(source, maxChannels, maxBuses, &b)) return false;
    switch (kind)
    {
        case StripKind::Channel: return false;
        case StripKind::Bus:     return b < stripIndex;
        case StripKind::Return:  return true;
    }
    return false;
}

// Layer `removed` was deleted and every layer above it shifted down by one.
// A strip that ducked from the deleted layer now ducks from nothing.
inline int sidechainAfterRemove(int source, int removed, int maxChannels) noexcept
{
    if (source < 0 || source >= maxChannels) return source;
    if (source == removed) return -1;
    return source > removed ? source - 1 : source;
}

// Layers `a` and `b` exchanged places: a strip that ducked from one now ducks from the other.
inline int sidechainAfterSwap(int source, int a, int b, int maxChannels) noexcept
{
    if (source < 0 || source >= maxChannels) return source;
    if (source == a) return b;
    if (source == b) return a;
    return source;
}

// The APVTS "scSrc" parameter encodes 0 = off, 1..N = channel 0..N-1, N+1 = external bus, N+2+b = group bus b.
inline int sidechainParamAfterRemove(int param, int removed, int maxChannels) noexcept
{
    return sidechainAfterRemove(param - 1, removed, maxChannels) + 1;
}
inline int sidechainParamAfterSwap(int param, int a, int b, int maxChannels) noexcept
{
    return sidechainAfterSwap(param - 1, a, b, maxChannels) + 1;
}

} // namespace mu_mix
