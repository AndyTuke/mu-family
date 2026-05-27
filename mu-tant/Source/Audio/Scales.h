#pragma once

#include <array>
#include <cmath>

// mu-tant scale-quantised pitch (design-voice.md "Pitch — scale-quantised").
// Lives in mu-tant (not mu-core) because mu-clid has no use for scales.
//
// A `tone` is a continuous scale-degree position: floor(tone) selects a degree,
// the fraction glides between adjacent degrees in MIDI space (perceptually-even
// glissando, logarithmic in frequency). Step modulators land on integer tones
// (clean scale jumps); smooth modulators glide through the fractions.
namespace mu_tant
{

struct Scale
{
    const char*               name;
    std::array<int, 12>       offsets;   // semitone offsets within one octave
    int                       count;     // degrees per octave (<= 12)
};

// Initial set (design-voice.md). Order is the dropdown order.
inline constexpr std::array<Scale, 12> kScales = {{
    { "Major",            {{0,2,4,5,7,9,11,0,0,0,0,0}}, 7 },
    { "Minor",            {{0,2,3,5,7,8,10,0,0,0,0,0}}, 7 },
    { "Dorian",           {{0,2,3,5,7,9,10,0,0,0,0,0}}, 7 },
    { "Phrygian",         {{0,1,3,5,7,8,10,0,0,0,0,0}}, 7 },
    { "Lydian",           {{0,2,4,6,7,9,11,0,0,0,0,0}}, 7 },
    { "Mixolydian",       {{0,2,4,5,7,9,10,0,0,0,0,0}}, 7 },
    { "Locrian",          {{0,1,3,5,6,8,10,0,0,0,0,0}}, 7 },
    { "Harmonic Minor",   {{0,2,3,5,7,8,11,0,0,0,0,0}}, 7 },
    { "Pentatonic Major", {{0,2,4,7,9,0,0,0,0,0,0,0}},  5 },
    { "Pentatonic Minor", {{0,3,5,7,10,0,0,0,0,0,0,0}}, 5 },
    { "Blues",            {{0,3,5,6,7,10,0,0,0,0,0,0}}, 6 },
    { "Chromatic",        {{0,1,2,3,4,5,6,7,8,9,10,11}}, 12 },
}};

inline constexpr int kNumScales = (int) kScales.size();

inline int clampScaleIdx(int i) noexcept
{
    return i < 0 ? 0 : (i >= kNumScales ? kNumScales - 1 : i);
}

// Semitone offset of scale degree `t` (can exceed one octave: degree 8 in a
// 7-note scale = degree 1 an octave up).
inline float scaleSemitone(const Scale& s, int t) noexcept
{
    const int n   = s.count;
    int       idx = t % n;
    int       oct = t / n;
    if (idx < 0) { idx += n; --oct; }   // floor-division for negative tones
    return (float) s.offsets[(size_t) idx] + (float) oct * 12.0f;
}

// Continuous tone -> MIDI note. `root` 0..11 (C..B), `octave` 0..8, `fine` cents.
inline float toneToMidi(int scaleIdx, int root, int octave, float tone, float fineCents) noexcept
{
    const Scale& s = kScales[(size_t) clampScaleIdx(scaleIdx)];
    const int    t0 = (int) std::floor(tone);
    const float  fr = tone - (float) t0;
    const float  semi = scaleSemitone(s, t0) + fr * (scaleSemitone(s, t0 + 1) - scaleSemitone(s, t0));
    return (float) root + 12.0f * (float) octave + semi + fineCents * 0.01f;
}

inline float midiToFreq(float midi) noexcept
{
    return 440.0f * std::pow(2.0f, (midi - 69.0f) / 12.0f);
}

} // namespace mu_tant
