#pragma once

#include "Audio/MusicalScales.h"   // mu-core: the scale table
#include <algorithm>
#include <array>

// ChordVoicing — which notes a Chord step plays. A step names a root (a degree of the master scale, in the master key),
// an octave, a chord, an inversion and a spread; this turns them into MIDI notes. Pure maths, no state: shared by the
// engine (what sounds) and the editor (note names), and unit-tested headless.
namespace mu_on
{

struct ChordDef
{
    const char*         name;        // the dropdown text
    const char*         shortName;   // what a step cell shows under its root
    std::array<int, 6>  semitones;   // above the root, ascending (not used by "Scale 7th")
    int                 count;
};

// Stored by index (saved in the step row), so the order only ever has entries appended.
inline constexpr int kNumChords = 18;
inline constexpr std::array<ChordDef, kNumChords> kChords = {{
    { "Scale 7th", "7th",  { 0, 0, 0, 0, 0, 0 },    4 },   // stacked thirds of the scale: the chord the key gives this degree
    { "Major",     "maj",  { 0, 4, 7, 0, 0, 0 },    3 },
    { "Minor",     "min",  { 0, 3, 7, 0, 0, 0 },    3 },
    { "Sus2",      "sus2", { 0, 2, 7, 0, 0, 0 },    3 },
    { "Sus4",      "sus4", { 0, 5, 7, 0, 0, 0 },    3 },
    { "Maj7",      "maj7", { 0, 4, 7, 11, 0, 0 },   4 },
    { "Min7",      "m7",   { 0, 3, 7, 10, 0, 0 },   4 },
    { "Dom7",      "7",    { 0, 4, 7, 10, 0, 0 },   4 },
    { "Min9",      "m9",   { 0, 3, 7, 10, 14, 0 },  5 },
    { "Maj9",      "maj9", { 0, 4, 7, 11, 14, 0 },  5 },
    { "Dom9",      "9",    { 0, 4, 7, 10, 14, 0 },  5 },
    { "Min11",     "m11",  { 0, 3, 7, 10, 14, 17 }, 6 },
    { "6th",       "6",    { 0, 4, 7, 9, 0, 0 },    4 },
    { "Min6",      "m6",   { 0, 3, 7, 9, 0, 0 },    4 },
    { "Add9",      "add9", { 0, 4, 7, 14, 0, 0 },   4 },
    { "Dim",       "dim",  { 0, 3, 6, 9, 0, 0 },    4 },
    { "Aug",       "aug",  { 0, 4, 8, 0, 0, 0 },    3 },
    { "Power",     "5",    { 0, 7, 12, 0, 0, 0 },   3 },
}};
inline constexpr int kDefaultChord = 6;   // Min7: the dub-techno staple

inline constexpr int kMaxChordInversion = 5;
inline constexpr int kMaxChordSpread = 4;

// A step's chord settings, as the engine receives them.
struct ChordHit
{
    int chord = kDefaultChord, inversion = 0, spread = 0, degree = 0, octave = 0;
};

struct ChordNotes
{
    int count = 0;
    std::array<int, 6> midi {};
};

// The notes of `hit` in the key `keyRoot` (0..11, C..B) and scale `scaleIndex`. The root is scale degree `degree` (0 = the
// key note) of the scale, in the octave below middle C plus `octave`. The chord's tones are then moved: each inversion
// lifts the lowest tone an octave, and Spread lifts the tone at position i by floor(i x spread / 2) more octaves, so
// 0 is close position and 4 puts every tone two octaves above the one before. A chromatic scale has no chord of its own,
// so "Scale 7th" is a minor seventh there.
inline ChordNotes voiceChord(int keyRoot, int scaleIndex, const ChordHit& hit) noexcept
{
    const auto& scale = mu_audio::kScales[(size_t) mu_audio::clampScaleIndex(scaleIndex)];
    const int degree = std::clamp(hit.degree, 0, scale.count - 1);
    const int rootMidi = 48 + keyRoot + (int) mu_audio::scaleSemitone(scale, degree) + 12 * hit.octave;

    int type = std::clamp(hit.chord, 0, kNumChords - 1);
    std::array<int, 6> tones {};
    int n = kChords[(size_t) type].count;
    if (type == 0 && scale.count < 12)
    {
        for (int k = 0; k < 4; ++k)
            tones[(size_t) k] = (int) (mu_audio::scaleSemitone(scale, degree + 2 * k) - mu_audio::scaleSemitone(scale, degree));
    }
    else
    {
        if (type == 0) { type = kDefaultChord; n = kChords[(size_t) type].count; }
        tones = kChords[(size_t) type].semitones;
    }

    for (int i = 0; i < std::clamp(hit.inversion, 0, n - 1); ++i)
    {
        tones[0] += 12;
        std::sort(tones.begin(), tones.begin() + n);
    }
    const int spread = std::clamp(hit.spread, 0, kMaxChordSpread);
    ChordNotes out;
    out.count = n;
    for (int i = 0; i < n; ++i)
        out.midi[(size_t) i] = std::clamp(rootMidi + tones[(size_t) i] + 12 * ((i * spread) / 2), 12, 120);
    return out;
}

// The pitch-class name of a step's root ("A", "F#"), for the step cell and the Note list.
inline juce::String chordRootName(int keyRoot, int scaleIndex, int degree)
{
    const auto& scale = mu_audio::kScales[(size_t) mu_audio::clampScaleIndex(scaleIndex)];
    const int midi = 48 + keyRoot + (int) mu_audio::scaleSemitone(scale, std::clamp(degree, 0, scale.count - 1));
    const auto name = mu_audio::midiNoteName(midi);
    return name.dropLastCharacters(1);   // the octave digit
}

} // namespace mu_on
