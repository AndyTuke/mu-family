#pragma once

// Sidechain sources name a mixer channel (a layer's strip). When the layers are renumbered — one is
// removed and the ones above shift down, or two are swapped — every source that pointed at a moved
// layer has to be re-pointed, or a strip ducks from the wrong layer (or from itself). These are the
// pure renumbering rules; the products apply them to their own storage (the mixer engine's atomics
// or the APVTS parameters).
//
// A source is a plain int: -1 = off, 0..maxChannels-1 = a channel, anything >= maxChannels = the
// external DAW bus. Off and the external bus never move.
namespace mu_mix
{

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

// The APVTS "scSrc" parameter encodes 0 = off, 1..N = channel 0..N-1, N+1 = external bus.
inline int sidechainParamAfterRemove(int param, int removed, int maxChannels) noexcept
{
    return sidechainAfterRemove(param - 1, removed, maxChannels) + 1;
}
inline int sidechainParamAfterSwap(int param, int a, int b, int maxChannels) noexcept
{
    return sidechainAfterSwap(param - 1, a, b, maxChannels) + 1;
}

} // namespace mu_mix
