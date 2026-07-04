#pragma once

#include <array>

// mu-toni chord library. A chord is a named type defined by ABSOLUTE intervals
// (semitones from the root) — see docs/mu-toni/design-sequencer.md "Chord model".
// The chord only selects the note pool to arpeggiate (mu-toni is monophonic).
// Ordered common -> exotic in tiers; the UI groups them with dividers.
namespace mu_toni
{

// Max chord tones: a 13th chord has up to 7 (1 3 5 7 9 11 13).
inline constexpr int kMaxChordTones = 7;

struct Chord
{
    const char*                        name;      // dropdown label
    const char*                        symbol;    // short symbol
    std::array<int, kMaxChordTones>    intervals; // semitones from root, ascending
    int                                count;     // number of tones
    int                                tier;      // 1..7 (dropdown grouping)
};

// Tier 1 Essentials · 2 Triads · 3 Sixths+core 7ths · 4 Extended 7ths ·
// 5 Ninths · 6 11ths/13ths · 7 Altered.
inline constexpr std::array<Chord, 35> kChords = {{
    // Tier 1 — Essentials
    { "Single note",  "1",       {{0}},               1, 1 },
    { "Power (5th)",  "5",       {{0,7}},             2, 1 },
    { "Major",        "maj",     {{0,4,7}},           3, 1 },
    { "Minor",        "min",     {{0,3,7}},           3, 1 },
    // Tier 2 — Common triads
    { "Suspended 2nd","sus2",    {{0,2,7}},           3, 2 },
    { "Suspended 4th","sus4",    {{0,5,7}},           3, 2 },
    { "Diminished",   "dim",     {{0,3,6}},           3, 2 },
    { "Augmented",    "aug",     {{0,4,8}},           3, 2 },
    // Tier 3 — Sixths & core sevenths
    { "Major 6th",    "6",       {{0,4,7,9}},         4, 3 },
    { "Minor 6th",    "m6",      {{0,3,7,9}},         4, 3 },
    { "Dominant 7th", "7",       {{0,4,7,10}},        4, 3 },
    { "Major 7th",    "maj7",    {{0,4,7,11}},        4, 3 },
    { "Minor 7th",    "m7",      {{0,3,7,10}},        4, 3 },
    // Tier 4 — Extended sevenths
    { "Minor 7 b5",   "m7b5",    {{0,3,6,10}},        4, 4 },
    { "Diminished 7th","dim7",   {{0,3,6,9}},         4, 4 },
    { "Minor-major 7","mMaj7",   {{0,3,7,11}},        4, 4 },
    { "Dom 7 sus4",   "7sus4",   {{0,5,7,10}},        4, 4 },
    // Tier 5 — Ninths
    { "Add 9",        "add9",    {{0,4,7,14}},        4, 5 },
    { "Minor add 9",  "m(add9)", {{0,3,7,14}},        4, 5 },
    { "Dominant 9th", "9",       {{0,4,7,10,14}},     5, 5 },
    { "Major 9th",    "maj9",    {{0,4,7,11,14}},     5, 5 },
    { "Minor 9th",    "m9",      {{0,3,7,10,14}},     5, 5 },
    { "Six-nine",     "6/9",     {{0,4,7,9,14}},      5, 5 },
    // Tier 6 — Elevenths & thirteenths
    { "Dominant 11th","11",      {{0,7,10,14,17}},    5, 6 },  // 3rd omitted (playability)
    { "Minor 11th",   "m11",     {{0,3,7,10,14,17}},  6, 6 },
    { "Major 11th",   "maj11",   {{0,4,7,11,14,17}},  6, 6 },
    { "Dominant 13th","13",      {{0,4,7,10,14,21}},  6, 6 },  // 11th omitted
    { "Minor 13th",   "m13",     {{0,3,7,10,14,21}},  6, 6 },
    { "Major 13th",   "maj13",   {{0,4,7,11,14,21}},  6, 6 },
    // Tier 7 — Altered / colour
    { "Dom 7 b9",     "7b9",     {{0,4,7,10,13}},     5, 7 },
    { "Dom 7 #9",     "7#9",     {{0,4,7,10,15}},     5, 7 },
    { "Dom 7 #11",    "7#11",    {{0,4,7,10,18}},     5, 7 },
    { "Dom 7 b5",     "7b5",     {{0,4,6,10}},        4, 7 },
    { "Dom 7 #5",     "7#5",     {{0,4,8,10}},        4, 7 },
    { "Major 7 #11",  "maj7#11", {{0,4,7,11,18}},     5, 7 },
}};

inline constexpr int kNumChords = (int) kChords.size();

inline int clampChordIndex(int i) noexcept
{
    return i < 0 ? 0 : (i >= kNumChords ? kNumChords - 1 : i);
}

} // namespace mu_toni
