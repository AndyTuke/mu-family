#pragma once

#include <array>
#include <cmath>

// mu-toni scale table. Copied into mu_toni:: (a product can't include another
// product's headers — family boundary rule). The arp uses absolute chords, so the
// scale's job here is (a) the Root palette and (b) the optional diatonic-snap of
// chord tones; it does not set chord quality. Mirrors mu-tant/Source/Audio/Scales.h.
namespace mu_toni
{

struct Scale
{
    const char*         name;
    std::array<int, 12> offsets;   // semitone offsets within one octave
    int                 count;     // degrees per octave (<= 12)
};

// Dropdown order. Same set as the rest of the family.
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

inline int clampScaleIndex(int i) noexcept
{
    return i < 0 ? 0 : (i >= kNumScales ? kNumScales - 1 : i);
}

// Snap a semitone offset above the root to the nearest scale tone (diatonic-snap).
// Works across octaves: the octave is preserved, only the within-octave pitch snaps.
inline int snapToScale(int semitoneAboveRoot, int scaleIndex) noexcept
{
    const Scale& s = kScales[(size_t) clampScaleIndex(scaleIndex)];
    int oct    = semitoneAboveRoot / 12;
    int within = semitoneAboveRoot % 12;
    if (within < 0) { within += 12; --oct; }

    int best = s.offsets[0];
    int bestDist = 128;
    for (int i = 0; i < s.count; ++i)
    {
        const int d = std::abs(within - s.offsets[(size_t) i]);
        if (d < bestDist) { bestDist = d; best = s.offsets[(size_t) i]; }
    }
    // Also consider the root an octave up (offset 12) — nearest may wrap.
    if (std::abs(within - 12) < bestDist) best = 12;

    return oct * 12 + best;
}

inline float midiToFreq(float midi) noexcept
{
    return 440.0f * std::pow(2.0f, (midi - 69.0f) / 12.0f);
}

} // namespace mu_toni
